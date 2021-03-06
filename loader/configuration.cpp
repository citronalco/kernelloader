/* Copyright (c) 2007 Mega Man */
#include <vector>

#include "configuration.h"
#include "graphic.h"
#include "fileXio_rpc.h"
#include "rom.h"
#include "loadermenu.h"

#define MAXIMUM_CONFIG_LENGTH 2048

typedef enum {
	CLASS_CONFIG_CHECK,
	CLASS_CONFIG_TEXT,
	CLASS_CONFIG_VIDEO,
} configuration_class_type_t;

class ConfigurationItem {
	protected:
	const char *name;
	configuration_class_type_t type;

	public:
	  ConfigurationItem(const char *name,
		configuration_class_type_t type):name(name), type(type) {
	} configuration_class_type_t getType(void) {
		return type;
	}

	const char *getName(void) {
		return name;
	}
};

class ConfigurationCheckItem:ConfigurationItem {
	protected:
	int *value;

	public:
	ConfigurationCheckItem(const char *name,
		int *value):ConfigurationItem(name, CLASS_CONFIG_CHECK), value(value) {
	}
	int writeData(FILE * fd) {
		static char text[MAXIMUM_CONFIG_LENGTH];
		int len;

		//kprintf("write: %s=%d 0x%x\n", name, *value, value);
		len = snprintf(text, MAXIMUM_CONFIG_LENGTH, "%s=%d\n", name, *value);
		return fwrite(text, 1, len, fd);
	}

	void readData(const char *buffer) {
		//kprintf("read: %s=%s 0x%x\n", name, buffer, value);
		*value = atoi(buffer);
	}
};

class ConfigurationVideoModeItem:ConfigurationItem {
	protected:
	int *value;

	public:
	ConfigurationVideoModeItem(const char *name,
		int *value):ConfigurationItem(name, CLASS_CONFIG_VIDEO), value(value) {
	}
	int writeData(FILE * fd) {
		static char text[MAXIMUM_CONFIG_LENGTH];
		int len;

		//kprintf("write: %s=%d 0x%x\n", name, *value, value);
		len = snprintf(text, MAXIMUM_CONFIG_LENGTH, "%s=%d\n", name, *value);
		return fwrite(text, 1, len, fd);
	}

	void readData(const char *buffer) {
		//kprintf("read: %s=%s 0x%x\n", name, buffer, value);
		*value = atoi(buffer);
		configureVideoParameter();
	}
};

class ConfigurationTextItem:ConfigurationItem {
	protected:
	char *value;
	int maxlen;

	public:
	  ConfigurationTextItem(const char *name, char *value,
		int maxlen):ConfigurationItem(name, CLASS_CONFIG_TEXT), value(value),
		maxlen(maxlen) {
	} int writeData(FILE * fd) {
		return fprintf(fd, "%s=%s\n", name, value);
	}

	void readData(const char *buffer) {
		strncpy(value, buffer, maxlen);
		value[maxlen - 1] = 0;
	}
};

std::vector < ConfigurationItem * >configurationVector;

extern "C" {

	int loadConfiguration(const char *configfile) {
		static char buffer[MAXIMUM_CONFIG_LENGTH];
		FILE *fd;

		fd = fopen(configfile, "rt");
		if (fd != NULL) {
			while (!feof(fd)) {
				char *val;

				if (fgets(buffer, MAXIMUM_CONFIG_LENGTH, fd) == NULL) {
					break;
				}
				val = strchr(buffer, '=');

				if (val != NULL) {
					std::vector < ConfigurationItem * >::iterator i;

					/* remove carriage return. */
					val[strlen(val) - 1] = 0;

					*val = 0;
					val++;
					for (i = configurationVector.begin();
						i != configurationVector.end(); i++) {
						const char *name = (*i)->getName();

						if (strcmp(buffer, name) == 0) {
							switch ((*i)->getType()) {
							case CLASS_CONFIG_CHECK:
								{
									ConfigurationCheckItem *item;

									item = (ConfigurationCheckItem *) * i;
									item->readData(val);
								}
								break;
							case CLASS_CONFIG_TEXT:
								{
									ConfigurationTextItem *item;

									item = (ConfigurationTextItem *) * i;
									item->readData(val);
								}
								break;
							case CLASS_CONFIG_VIDEO:
								{
									ConfigurationVideoModeItem *item;

									item = (ConfigurationVideoModeItem *) * i;
									item->readData(val);
								}
								break;
							default:
								error_printf("Unsupported configuration type.");
								break;
							}
						}
					}
				}
			}
			fclose(fd);
			return 0;
		} else {
			return -1;
		}
	}

	void saveMcIcons(const char *config_dir)
	{
		FILE *fd;

		const char *mcicons[] = {
			"icon.sys",
			"kloader.icn",
		};
		const rom_entry_t *romfile;
		char path[40];
		unsigned int n;

		/* Copy icons to memory card. */
		for (n = 0; n < (sizeof(mcicons)/sizeof(mcicons[0])); n++) {
			snprintf(path, sizeof(path), "mcicons/%s", mcicons[n]);
			romfile = rom_getFile(path);
			if (romfile != NULL) {
				snprintf(path, sizeof(path), "%s/%s", config_dir, mcicons[n]);
				fd = fopen(path, "rb");
				if (fd == NULL) {
					/* Create memory card directory. */
					fileXioMkdir(config_dir, 0777);
					fd = fopen(path, "rb");
				}
				if (fd == NULL) {
					fd = fopen(path, "wb");
					if (fd != NULL) {
						const unsigned char *buffer;
						int rv;
						unsigned int size;

						buffer = (const unsigned char *)romfile->start;
						size = romfile->size;
						do {
							rv = fwrite(buffer, 1, size, fd);
							if (rv < 0) {
								error_printf("Failed to create icons on MC0.");
								break;
							}
							size -= rv;
							buffer += rv;
						} while(size > 0);
						fclose(fd);
						if (rv < 0) {
							break;
						}
					}
				} else {
					/* Icon is alreadystored on the memory card. */
					fclose(fd);
				}
			} else {
				error_printf("ROM-File %s is missing.", path);
			}
		}
	}

	void saveConfiguration(const char *configfile) {
		FILE *fd;
		int n;

		n = strlen(configfile);
		if (n > 4) {
			/* Check if configuration is saved on memory card. */
			if (((configfile[0] == 'm') || (configfile[0] == 'M'))
				&& ((configfile[1] == 'c') || (configfile[1] == 'C'))
				&& ((configfile[2] == '0') || (configfile[2] == '1'))
				&& (configfile[3] == ':')) {
				/* Configuration is saved on memory card. */
				n = 4;
				while(configfile[n] != 0) {
					if (configfile[n] != '/') {
						break;
					}
					n++;
				}
				if (strncasecmp(configfile + n, "kloader/", 8) == 0) {
					/* Configuration is saved in kloader directory. */
					if (configfile[2] == '0') {
						/* First memory card. */
						saveMcIcons(CONFIG_DIR);
					} else if (configfile[2] == '1') {
						/* Second memory card. */
						saveMcIcons(CONFIG_DIR2);
					}
				}
			}
		}
		fd = fopen(configfile, "wt");
		if (fd != NULL) {
			std::vector < ConfigurationItem * >::iterator i;

			for (i = configurationVector.begin();
				i != configurationVector.end(); i++) {
				switch ((*i)->getType()) {
				case CLASS_CONFIG_CHECK:
					{
						ConfigurationCheckItem *item;

						item = (ConfigurationCheckItem *) * i;
						item->writeData(fd);
					}
					break;
				case CLASS_CONFIG_TEXT:
					{
						ConfigurationTextItem *item;

						item = (ConfigurationTextItem *) * i;
						item->writeData(fd);
					}
					break;
				case CLASS_CONFIG_VIDEO:
					{
						ConfigurationVideoModeItem *item;

						item = (ConfigurationVideoModeItem *) * i;
						item->writeData(fd);
					}
					break;
				default:
					error_printf("Unsupported configuration type.");
					break;
				}
			}
			fclose(fd);
		} else {
			error_printf("Failed to save configuration on MC0. Please insert memory card.");
		}
	}

}

static void addConfigurationItem(ConfigurationItem * item)
{
	configurationVector.push_back(item);
}

void addConfigCheckItem(const char *name, int *value)
{
	ConfigurationCheckItem *item;

	item = new ConfigurationCheckItem(name, value);
	//kprintf("name %s value 0x%x\n", name, value);

	addConfigurationItem((ConfigurationItem *) item);
}

void addConfigVideoItem(const char *name, int *value)
{
	ConfigurationVideoModeItem *item;

	item = new ConfigurationVideoModeItem(name, value);
	//kprintf("name %s value 0x%x\n", name, value);

	addConfigurationItem((ConfigurationItem *) item);
}

void addConfigTextItem(const char *name, char *value, int maxlen)
{
	ConfigurationTextItem *item;

	item = new ConfigurationTextItem(name, value, maxlen);

	addConfigurationItem((ConfigurationItem *) item);
}
