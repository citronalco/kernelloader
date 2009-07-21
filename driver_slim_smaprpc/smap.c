/*
 *  smap.c -- PlayStation 2 Ethernet device driver
 *
 *	Copyright (C) 2001, 2002  Sony Computer Entertainment Inc.
 *	Copyright (C) 2009 Mega Man
 *
 *  This file is subject to the terms and conditions of the GNU General
 *  Public License Version 2. See the file "COPYING" in the main
 *  directory of this archive for more details.
 *
 *  This driver replaces the original smap.c in linux 2.4.17
 *  This driver is intended to be used with the slim PSTwo
 *  and the smaprpc.irx module of kernelloader.
 */

#if defined(linux)

#include "smap.h"

#include <asm/ps2/sifdefs.h>
#include <asm/ps2/sbcall.h>

#define SMAP_BIND_RPC_ID 0x0815e000

#define SMAP_CMD_SEND 1
#define SMAP_CMD_SET_BUFFER 2
#define SMAP_CMD_GET_MAC_ADDR 3

#define SIF_SMAP_RECEIVE 0x07

typedef struct t_SifCmdHeader
{
	u32 size;
	void *dest;
	int cid;
	u32 unknown;
} SifCmdHeader_t;

typedef struct {
	struct t_SifCmdHeader    sifcmd;
	u32 payload;
    u32 size;
} iop_sifCmdSmapIrq_t;

struct smap_chan *smap_chan = NULL;
u32 smap_rpc_data[2048] __attribute__((aligned(16)));

/*--------------------------------------------------------------------------*/

static int  smap_start_xmit(struct sk_buff *skb, struct net_device *net_dev);
static struct net_device_stats * smap_get_stats(struct net_device *net_dev);
static int  smap_open(struct net_device *net_dev);
static int  smap_close(struct net_device *net_dev);
static int  smap_ioctl(struct net_device *net_dev, struct ifreq *ifr, int cmd);

static void smap_rpcend_notify(void *arg);
static void smap_rpc_setup(struct smap_chan *smap);

static int smap_thread(void *arg);
static void smap_run(struct smap_chan *smap);
static void  smap_start_xmit2(struct smap_chan *smap);

static void smap_skb_queue_init(struct smap_chan *smap, struct sk_buff_head *head);
static void smap_skb_enqueue(struct sk_buff_head *head, struct sk_buff *newsk);
static void smap_skb_enqueue(struct sk_buff_head *head, struct sk_buff *newsk);
static struct sk_buff *smap_skb_dequeue(struct sk_buff_head *head);

/*--------------------------------------------------------------------------*/

static void smap_skb_queue_init(struct smap_chan *smap, struct sk_buff_head *head)
{
	unsigned long flags;

	spin_lock_irqsave(&smap->spinlock, flags);
	(void)skb_queue_head_init(head);
	spin_unlock_irqrestore(&smap->spinlock, flags);

	return;
}

static void smap_skb_enqueue(struct sk_buff_head *head, struct sk_buff *newsk)
{
	(void)skb_queue_tail(head, newsk);
	return;
}

static void smap_skb_requeue(struct sk_buff_head *head, struct sk_buff *newsk)
{
	(void)skb_queue_head(head, newsk);
	return;
}

static struct sk_buff *smap_skb_dequeue(struct sk_buff_head *head)
{
	struct sk_buff *skb;

	skb = skb_dequeue(head);
	return(skb);
}

/*--------------------------------------------------------------------------*/

/* return value: 0 if success, !0 if error */
static int
smap_start_xmit(struct sk_buff *skb, struct net_device *net_dev)
{
	struct smap_chan *smap = net_dev->priv;
	unsigned long flags;

	spin_lock_irqsave(&smap->spinlock, flags);

	smap_skb_enqueue(&smap->txqueue, skb);
	wake_up_interruptible(&smap->wait_smaprun);
	spin_unlock_irqrestore(&smap->spinlock, flags);
	return(0);
}

/*--------------------------------------------------------------------------*/

static struct net_device_stats *
smap_get_stats(struct net_device *net_dev)
{
	struct smap_chan *smap = net_dev->priv;

	return(&smap->net_stats);
}

static void smap_run(struct smap_chan *smap)
{
	unsigned long flags;

	spin_lock_irqsave(&smap->spinlock, flags);
	while (smap->txqueue.qlen > 0) {
		spin_unlock_irqrestore(&smap->spinlock, flags);
		smap_start_xmit2(smap);
		spin_lock_irqsave(&smap->spinlock, flags);
	}
	spin_unlock_irqrestore(&smap->spinlock, flags);
}


static void smap_start_xmit2(struct smap_chan *smap)
{
	int rv;
	struct completion compl;
	struct sk_buff *skb;
	unsigned long flags;

	spin_lock_irqsave(&smap->spinlock, flags);
	skb = smap_skb_dequeue(&smap->txqueue);
	spin_unlock_irqrestore(&smap->spinlock, flags);
	if (skb == NULL)
		return;

	init_completion(&compl);

	down(&smap->smap_rpc_sema);
	memcpy(smap_rpc_data, skb->data, skb->len);
	do {
		rv = ps2sif_callrpc(&smap->cd_smap_rpc, SMAP_CMD_SEND,
			SIF_RPCM_NOWAIT,
			(void *) smap_rpc_data, skb->len,
			smap_rpc_data, sizeof(smap_rpc_data),
			(ps2sif_endfunc_t)smap_rpcend_notify,
			(void *)&compl);
	} while (rv == -E_SIF_PKT_ALLOC);
	if (rv != 0) {
		printk("%s: smap_start_xmit2: callrpc failed, (%d)\n", smap->net_dev->name, rv);

		spin_lock_irqsave(&smap->spinlock, flags);
		smap_skb_requeue(&smap->txqueue, skb);
		spin_unlock_irqrestore(&smap->spinlock, flags);
	} else {
		wait_for_completion(&compl);

		dev_kfree_skb(skb);
	}
	up(&smap->smap_rpc_sema);
}

static int
smap_open(struct net_device *net_dev)
{
	struct smap_chan *smap = net_dev->priv;

	smap->flags |= SMAP_F_OPENED;
	smap_skb_queue_init(smap, &smap->txqueue);

	return(0);	/* success */
}

static int
smap_close(struct net_device *net_dev)
{
	struct smap_chan *smap = net_dev->priv;
	unsigned long flags;

	spin_lock_irqsave(&smap->spinlock, flags);
	smap->flags &= ~SMAP_F_OPENED;

	spin_unlock_irqrestore(&smap->spinlock, flags);

	return(0);	/* success */
}

/*--------------------------------------------------------------------------*/

static int
smap_ioctl(struct net_device *net_dev, struct ifreq *ifr, int cmd)
{
	struct smap_chan *smap = net_dev->priv;
	int retval = 0;

	printk("%s: PlayStation 2 SMAP ioctl %d\n", net_dev->name, cmd);

	switch (cmd) {
	default:
		retval = -EOPNOTSUPP;
		break;
	}

	return(retval);
}

static void smap_rpc_setup(struct smap_chan *smap)
{
	int loop;
	int rv;
	volatile int j;
	struct completion compl;

	if (smap->rpc_initialized) {
		return;
	}
	init_completion(&compl);

	/* bind smaprpc.irx module */
	for (loop = 100; loop; loop--) {
		rv = ps2sif_bindrpc(&smap->cd_smap_rpc, SMAP_BIND_RPC_ID,
			SIF_RPCM_NOWAIT, smap_rpcend_notify, (void *)&compl);
		if (rv < 0) {
			printk("%s: smap rpc setup: bind rv = %d.\n", smap->net_dev->name, rv);
			break;
		}
		wait_for_completion(&compl);
		if (smap->cd_smap_rpc.serve != 0)
			break;
		j = 0x010000;
		while (j--) ;
	}
	if (smap->cd_smap_rpc.serve == 0) {
		printk("%s: smap rpc setup: bind error 1, network will not work on slim PSTwo\n", smap->net_dev->name);
		return;
	}

	memset(smap_rpc_data, 0, 32);
	do {
		rv = ps2sif_callrpc(&smap->cd_smap_rpc, SMAP_CMD_GET_MAC_ADDR,
			SIF_RPCM_NOWAIT,
			(void *) smap_rpc_data, 32,
			smap_rpc_data, sizeof(smap_rpc_data),
			(ps2sif_endfunc_t)smap_rpcend_notify,
			(void *)&compl);
	} while (rv == -E_SIF_PKT_ALLOC);
	if (rv != 0) {
		printk("%s: SMAP_CMD_GET_MAC_ADDR failed, (%d)\n", smap->net_dev->name, rv);
	} else {
		wait_for_completion(&compl);
		memcpy(smap->net_dev->dev_addr, &smap_rpc_data[1], ETH_ALEN);
		printk("%s: MAC %02x:%02x:%02x:%02x:%02x:%02x\n", smap->net_dev->name,
			smap->net_dev->dev_addr[0],
			smap->net_dev->dev_addr[1],
			smap->net_dev->dev_addr[2],
			smap->net_dev->dev_addr[3],
			smap->net_dev->dev_addr[4],
			smap->net_dev->dev_addr[5]);
	}

	smap->shared_size = 32 * 1024;
	smap->shared_addr = kmalloc(smap->shared_size, GFP_KERNEL);
	if (smap->shared_addr != NULL) {
		smap_rpc_data[0] = virt_to_phys(smap->shared_addr);
		smap_rpc_data[1] = smap->shared_size;
		do {
			rv = ps2sif_callrpc(&smap->cd_smap_rpc, SMAP_CMD_SET_BUFFER,
				SIF_RPCM_NOWAIT,
				(void *) smap_rpc_data, 32,
				smap_rpc_data, 4,
				(ps2sif_endfunc_t)smap_rpcend_notify,
				(void *)&compl);
		} while (rv == -E_SIF_PKT_ALLOC);
		if (rv != 0) {
			printk("%s: SMAP_CMD_SET_BUFFER failed, (%d). Receive will not work.\n", smap->net_dev->name, rv);
		} else {
			wait_for_completion(&compl);
			if (smap_rpc_data[0] != 0) {
				printk("%s: SMAP_CMD_SET_BUFFER failed, (%d). Receive will not work.\n", smap->net_dev->name, smap_rpc_data[0]);
			}
		}
	} else {
		printk("%s: Failed to allocate receive buffer. Receive will not work.\n", smap->net_dev->name);
	}
	smap->rpc_initialized = -1;
}

static int smap_thread(void *arg)
{
	struct smap_chan *smap = (struct smap_chan *)arg;
	unsigned long flags;

	spin_lock_irqsave(&current->sigmask_lock, flags);
	siginitsetinv(&current->blocked,
			sigmask(SIGKILL)|sigmask(SIGINT)|sigmask(SIGTERM));
	recalc_sigpending(current);
	spin_unlock_irqrestore(&current->sigmask_lock, flags);

	lock_kernel();

	/* get rid of all our resources related to user space */
	daemonize();

	/* Set the name of this process. */
	sprintf(current->comm, "smap");		/* up to 16B */
        
	unlock_kernel();

	smap->smaprun_task = current;

	while (1) {
		wait_queue_t wait;
		init_waitqueue_entry(&wait, current);
		add_wait_queue(&smap->wait_smaprun, &wait);
		set_current_state(TASK_INTERRUPTIBLE);

		smap_run(smap);

		schedule();
		remove_wait_queue(&smap->wait_smaprun, &wait);
		if (signal_pending(current))
			break;
	}

	smap->smaprun_task = NULL;
	if (smap->smaprun_compl != NULL)
		complete(smap->smaprun_compl);	/* notify that we've exited */

	return(0);
}

static void smap_rpcend_notify(void *arg)
{
	complete((struct completion *)arg);
	return;
}

static void handleSmapIRQ(iop_sifCmdSmapIrq_t *pkt, void *arg)
{
	struct smap_chan *smap = (struct smap_chan *) arg;
	struct sk_buff *skb;
	u8 *data;
	int i;

	dma_cache_inv(pkt, sizeof(*pkt));
	data = phys_to_virt(pkt->payload);
	dma_cache_inv(data, pkt->size);
	
	skb = dev_alloc_skb(pkt->size + 2);
	if (skb == NULL) {
		printk("%s:handleSmapIRQ, skb alloc error\n",
					smap->net_dev->name);
		return;
	}
	skb_reserve(skb, 2);	/* 16 byte align the data fields */
	eth_copy_and_sum(skb, data, pkt->size, 0);
	skb_put(skb, pkt->size);
	skb->dev = smap->net_dev;
	skb->protocol = eth_type_trans(skb, smap->net_dev);
	smap->net_dev->last_rx = jiffies;
	netif_rx(skb);
}

int
smap_probe(struct net_device *net_dev)
{
	struct smap_chan *smap = NULL;
	int modflag = 0;
	struct sb_sifaddcmdhandler_arg addcmdhandlerparam;

	if (net_dev == NULL)
		modflag = 1;

	if (smap_chan != NULL) {
		printk("PlayStation 2 SMAP: already used\n");
		return(-ENODEV);
	}

	/* alloc & clear control structure */
	smap = kmalloc(sizeof(struct smap_chan), GFP_KERNEL);
	if (smap == NULL) {
		printk("PlayStation 2 SMAP: memory alloc error\n");
		return(-ENOMEM);
	}
	memset(smap, 0, sizeof(struct smap_chan));

	/* get & init network device structure */
	if (modflag) {
		net_dev = init_etherdev(NULL, 0);
		if (net_dev == NULL) {
			printk("PlayStation 2 SMAP: init_etherdev error\n");
			goto error;
		}
	} else {
		ether_setup(net_dev);
	}
	smap->net_dev = net_dev;
	net_dev->priv = smap;
	SET_MODULE_OWNER(net_dev);

	net_dev->open = smap_open;
	net_dev->stop = smap_close;
	net_dev->do_ioctl = smap_ioctl;
	net_dev->hard_start_xmit = smap_start_xmit;
	net_dev->get_stats = smap_get_stats;
	net_dev->set_mac_address = NULL;

	spin_lock_init(&smap->spinlock);
	init_MUTEX(&smap->smap_rpc_sema);
	init_waitqueue_head(&smap->wait_smaprun);

	addcmdhandlerparam.fid = SIF_SMAP_RECEIVE;
	addcmdhandlerparam.func = handleSmapIRQ;
	addcmdhandlerparam.data = (void *) smap;
	if (sbios(SB_SIFADDCMDHANDLER, &addcmdhandlerparam) < 0) {
		printk("Failed to initialize smap IRQ handler. Receive will not work.\n");
	}

	smap_rpc_setup(smap);

	if (smap->rpc_initialized) {
		kernel_thread(smap_thread, (void *)smap, 0);

		printk("PlayStation 2 SMAP(Ethernet) device driver.\n");

		return(0);	/* success */
	}
error:
	printk("PlayStation 2 SMAP(Ethernet) device not found.\n");
	if (smap) {
		kfree(smap);
	}
	smap_chan = NULL;
	return(-ENODEV);
}

void
smap_cleanup_module(void)
{
	struct smap_chan *smap = smap_chan;
	struct net_device *net_dev;

	if (smap == NULL) {
		printk("smap control structure error(null).\n");
		return;
	}

	if (smap->rpc_initialized) {
		/* Remove interrupt handler. */
		struct sb_sifremovecmdhandler_arg param;

		param.fid = SIF_SMAP_RECEIVE;
		if (sbios(SB_SIFREMOVECMDHANDLER, &param) < 0) {
			printk("Failed to remove smap IRQ handler.\n");
		}
	}

	if (smap->smaprun_task != NULL) {
		struct completion compl;

		init_completion(&compl);
		smap->smaprun_compl = &compl;
		send_sig(SIGKILL, smap->smaprun_task, 1);

		/* wait the thread exit */
		wait_for_completion(&compl);
		smap->smaprun_compl = NULL;
	}
	if (smap->shared_addr != NULL) {
		kfree(smap->shared_addr);
	}

	if (smap->net_dev == NULL)
		goto end;

	net_dev = smap->net_dev;

	if (net_dev->flags & IFF_UP)
		dev_close(net_dev);

	unregister_netdev(net_dev);

end:
	if (smap) {
		/* XXX: Disable device. */
		kfree(smap);
	}
	smap_chan = NULL;
	return;
}

int __init
smap_init_module(void)
{
	return(smap_probe(NULL));
}

module_init(smap_init_module);
module_exit(smap_cleanup_module);

MODULE_AUTHOR("Mega Man");
MODULE_DESCRIPTION("PlayStation 2 ethernet device driver");
MODULE_LICENSE("GPL");

/*--------------------------------------------------------------------------*/

#endif /* linux */
