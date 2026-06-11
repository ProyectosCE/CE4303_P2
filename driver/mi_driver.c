#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/if_ether.h>
#include <linux/etherdevice.h>
#include <linux/delay.h>
#include <linux/string.h>

#include <net/net_namespace.h>
#include <net/rtnetlink.h>

#define DEVICE_NAME "raspberry_usb"
#define IFACE_PREFIX "enx"
#define BLOCK_SIZE 1024
#define CUSTOM_ETHERTYPE 0x88B5

static int major_number;
static struct packet_type mi_packet_hook;

static struct net_device *find_target_dev(void)
{
    struct net_device *net_dev;
    struct net_device *found = NULL;

    rtnl_lock();

    for_each_netdev(&init_net, net_dev) {
        if (strncmp(net_dev->name, IFACE_PREFIX, sizeof(IFACE_PREFIX) - 1) == 0) {
            found = net_dev;
            dev_hold(found);
            break;
        }
    }

    rtnl_unlock();

    return found;
}

static int send_payload(struct net_device *dev, const void *payload, size_t payload_len)
{
    struct sk_buff *skb;
    struct ethhdr *eth;
    unsigned char *data;
    unsigned int frame_len;
    int ret;

    frame_len = ETH_HLEN + payload_len;

    skb = dev_alloc_skb(frame_len + LL_RESERVED_SPACE(dev));
    if (!skb)
        return -ENOMEM;

    skb_reserve(skb, LL_RESERVED_SPACE(dev));

    skb->dev = dev;

    eth = skb_put(skb, ETH_HLEN);

    eth_broadcast_addr(eth->h_dest);
    ether_addr_copy(eth->h_source, dev->dev_addr);
    eth->h_proto = htons(CUSTOM_ETHERTYPE);

    skb->protocol = eth->h_proto;
    skb_reset_mac_header(skb);

    data = skb_put(skb, payload_len);
    memcpy(data, payload, payload_len);

    ret = dev_queue_xmit(skb);

    if (ret != NET_XMIT_SUCCESS && ret != NET_XMIT_CN)
        return -EIO;

    return 0;
}

static ssize_t driver_write(
    struct file *file,
    const char __user *buffer,
    size_t len,
    loff_t *offset
)
{
    struct net_device *dev;
    char *kernel_buffer;
    size_t bytes_sent = 0;
    int ret = 0;
    static const char eof_msg[] = "FIN_TRANSMISION";

    pr_info("[Mi_Driver_Proyecto] Tamaño solicitado: %zu bytes\n", len);

    if (len == 0)
        return 0;

    dev = find_target_dev();
    if (!dev) {
        pr_err("[Mi_Driver_Proyecto] Error: interfaz con prefijo '%s' no encontrada.\n",
               IFACE_PREFIX);
        return -ENODEV;
    }

    kernel_buffer = kmalloc(BLOCK_SIZE, GFP_KERNEL);
    if (!kernel_buffer) {
        dev_put(dev);
        return -ENOMEM;
    }

    while (bytes_sent < len) {
        size_t chunk_size = min_t(size_t, BLOCK_SIZE, len - bytes_sent);

        if (copy_from_user(kernel_buffer, buffer + bytes_sent, chunk_size) != 0) {
            ret = -EFAULT;
            break;
        }

        ret = send_payload(dev, kernel_buffer, chunk_size);
        if (ret < 0)
            break;

        bytes_sent += chunk_size;

        usleep_range(100, 200);
    }

    if (ret == 0) {
        ret = send_payload(dev, eof_msg, sizeof(eof_msg) - 1);
        if (ret < 0)
            pr_warn("[Mi_Driver_Proyecto] No se pudo enviar marcador EOF.\n");
    }

    kfree(kernel_buffer);
    dev_put(dev);

    if (ret < 0)
        return ret;

    pr_info("[Mi_Driver_Proyecto] Transmisión completada: %zu bytes enviados.\n",
            bytes_sent);

    return len;
}

static const struct file_operations fops = {
    .owner = THIS_MODULE,
    .write = driver_write,
};

static int mi_packet_rcv(
    struct sk_buff *skb,
    struct net_device *dev,
    struct packet_type *pt,
    struct net_device *orig_dev
)
{
    if (dev && strncmp(dev->name, IFACE_PREFIX, sizeof(IFACE_PREFIX) - 1) == 0) {
        pr_info("[Mi_Driver_Proyecto] Paquete recibido en %s\n", dev->name);
    }

    kfree_skb(skb);
    return NET_RX_SUCCESS;
}

static int __init mi_driver_init(void)
{
    pr_info("[Mi_Driver_Proyecto] Inicializando driver...\n");

    major_number = register_chrdev(0, DEVICE_NAME, &fops);
    if (major_number < 0) {
        pr_err("[Mi_Driver_Proyecto] Falló register_chrdev.\n");
        return major_number;
    }

    pr_info("[Mi_Driver_Proyecto] Major asignado: %d\n", major_number);
    pr_info("[Mi_Driver_Proyecto] Crear nodo con: sudo mknod /dev/%s c %d 0\n",
            DEVICE_NAME, major_number);

    mi_packet_hook.type = htons(CUSTOM_ETHERTYPE);
    mi_packet_hook.func = mi_packet_rcv;
    dev_add_pack(&mi_packet_hook);

    return 0;
}

static void __exit mi_driver_exit(void)
{
    dev_remove_pack(&mi_packet_hook);
    unregister_chrdev(major_number, DEVICE_NAME);

    pr_info("[Mi_Driver_Proyecto] Driver descargado.\n");
}

module_init(mi_driver_init);
module_exit(mi_driver_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("PB Team");
MODULE_DESCRIPTION("Driver custom para transferencia de imagen por interfaz USB-Ethernet");

