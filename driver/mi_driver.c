c/*
Nombre archivo: mi_driver.c

Que hace: Módulo de kernel (Linux) que expone un character device
(/dev/raspberry_usb). Lo que se escriba a ese dispositivo se fragmenta
en bloques y se envía como tramas Ethernet crudas (EtherType custom
0x88B5) por la primera interfaz de red cuyo nombre empiece con "enx". 
Cada trama va envuelta en un header propio (pov_frame_header) que 
indica tipo (DATA/END) y longitud.
También registra un hook que intercepta tramas entrantes con ese
EtherType.
*/
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

#define DEVICE_NAME "raspberry_usb" // Nombre del character device creado
#define IFACE_PREFIX "enx"          // Prefijo de la interfaz USB-Ethernet objetivo
#define BLOCK_SIZE 1024             // Tamaño máximo de cada bloque enviado por trama
#define CUSTOM_ETHERTYPE 0x88B5     // EtherType propio para distinguir nuestras tramas

#define POV_FRAME_MAGIC 0x504F5631U /* ASCII: POV1 */
#define POV_FRAME_TYPE_DATA 1
#define POV_FRAME_TYPE_END  2

// Header que se antepone a cada trama enviada: identifica el protocolo (magic),
// el tipo de trama (DATA/END) y la longitud del payload que sigue.
struct pov_frame_header {
    __be32 magic;
    u8 type;
    __be16 length;
} __packed;

static int major_number; // Número major asignado al char device
static struct packet_type mi_packet_hook; // Hook para recibir tramas con CUSTOM_ETHERTYPE

/*
Nombre de funcion: find_target_dev
Que hace: Recorre la lista de interfaces de red del namespace por defecto
y devuelve la primera cuyo nombre empiece con IFACE_PREFIX ("enx"),
incrementando su contador de referencias (dev_hold).
Que recibe: Nada.
Que retorna:
  - Puntero al net_device encontrado (con referencia tomada, debe
    liberarse luego con dev_put).
  - NULL si no se encontró ninguna interfaz con ese prefijo.
Nota: usa rtnl_lock/unlock para iterar la lista de dispositivos de forma segura.
*/
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

/*
Nombre de funcion: send_frame
Que hace: Construye y transmite una trama Ethernet completa: cabecera
Ethernet (broadcast, EtherType custom) + pov_frame_header (magic, tipo,
longitud) + payload opcional. La envía con dev_queue_xmit.
Que recibe:
  - dev: interfaz de red por donde se transmite.
  - frame_type: POV_FRAME_TYPE_DATA o POV_FRAME_TYPE_END.
  - payload: puntero a los datos a incluir (puede ser NULL si payload_len es 0).
  - payload_len: tamaño del payload en bytes (máximo 65535).
Que retorna:
  - 0 si la trama se encoló/transmitió correctamente.
  - -EINVAL si payload_len > 65535.
  - -ENOMEM si no se pudo reservar el skb.
  - -EIO si dev_queue_xmit falló (excepto NET_XMIT_CN, que se considera ok).
*/
static int send_frame(
    struct net_device *dev,
    u8 frame_type,
    const void *payload,
    size_t payload_len
)
{
    struct sk_buff *skb;
    struct ethhdr *eth;
    struct pov_frame_header *hdr;
    unsigned char *data;
    unsigned int transport_len;
    unsigned int frame_len;
    int ret;

    if (payload_len > 65535)
        return -EINVAL;

    transport_len = sizeof(struct pov_frame_header) + payload_len;
    frame_len = ETH_HLEN + transport_len;

    // Reserva espacio extra para los headers de capa de enlace (LL_RESERVED_SPACE)
    skb = dev_alloc_skb(frame_len + LL_RESERVED_SPACE(dev));
    if (!skb)
        return -ENOMEM;

    skb_reserve(skb, LL_RESERVED_SPACE(dev));

    skb->dev = dev;

    // --- Cabecera Ethernet: destino broadcast, origen = MAC de la interfaz ---
    eth = skb_put(skb, ETH_HLEN);
    eth_broadcast_addr(eth->h_dest);
    ether_addr_copy(eth->h_source, dev->dev_addr);
    eth->h_proto = htons(CUSTOM_ETHERTYPE);

    skb->protocol = eth->h_proto;
    skb_reset_mac_header(skb);

    // --- Header propio (magic + tipo + longitud), todo en network byte order ---
    hdr = skb_put(skb, sizeof(struct pov_frame_header));
    hdr->magic = cpu_to_be32(POV_FRAME_MAGIC);
    hdr->type = frame_type;
    hdr->length = cpu_to_be16((u16)payload_len);

    // --- Payload (si hay) ---
    if (payload_len > 0) {
        data = skb_put(skb, payload_len);
        memcpy(data, payload, payload_len);
    }

    ret = dev_queue_xmit(skb);

    // NET_XMIT_CN (congestion notification) se trata como éxito
    if (ret != NET_XMIT_SUCCESS && ret != NET_XMIT_CN)
        return -EIO;

    return 0;
}

/*
Nombre de funcion: driver_write
Que hace: Handler de write() para /dev/raspberry_usb. Copia los datos
del usuario en bloques de hasta BLOCK_SIZE, y por cada bloque llama a
send_frame con tipo DATA. Al terminar todos los bloques, envía una
trama final de tipo END (payload vacío) para indicar fin de mensaje.
Que recibe:
  - file: estructura del archivo abierto (no usada directamente).
  - buffer: puntero a los datos en espacio de usuario a escribir.
  - len: cantidad de bytes a escribir.
  - offset: posición de escritura (no usado).
Que retorna:
  - len (cantidad de bytes recibidos) si todo se envió correctamente.
  - 0 si len == 0.
  - -ENODEV si no se encontró ninguna interfaz "enx*".
  - -ENOMEM si falla kmalloc del buffer intermedio.
  - -EFAULT si falla copy_from_user.
  - Código de error negativo propagado desde send_frame si falla el envío.
Nota: usa usleep_range(100, 200) entre tramas para no saturar la interfaz.
*/
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

    // Fragmenta "len" bytes en bloques de hasta BLOCK_SIZE y los envía uno a uno
    while (bytes_sent < len) {
        size_t chunk_size = min_t(size_t, BLOCK_SIZE, len - bytes_sent);

        if (copy_from_user(kernel_buffer, buffer + bytes_sent, chunk_size) != 0) {
            ret = -EFAULT;
            break;
        }

        ret = send_frame(dev, POV_FRAME_TYPE_DATA, kernel_buffer, chunk_size);
        if (ret < 0)
            break;

        bytes_sent += chunk_size;

        usleep_range(100, 200);
    }

    // Si todos los bloques DATA se enviaron bien, se envía la trama END (marca fin de mensaje)
    if (ret == 0) {
        ret = send_frame(dev, POV_FRAME_TYPE_END, NULL, 0);
        if (ret < 0)
            pr_warn("[Mi_Driver_Proyecto] No se pudo enviar frame EOF.\n");
    }

    kfree(kernel_buffer);
    dev_put(dev);

    if (ret < 0)
        return ret; // libera la referencia tomada en find_target_dev

    pr_info("[Mi_Driver_Proyecto] Transmisión completada: %zu bytes enviados.\n",
            bytes_sent);

    return len;
}

// Tabla de operaciones del char device: solo se implementa write()
static const struct file_operations fops = {
    .owner = THIS_MODULE,
    .write = driver_write,
};

/*
Nombre de funcion: mi_packet_rcv
Que hace: Callback del packet_type hook. Se ejecuta cuando llega una
trama con CUSTOM_ETHERTYPE por cualquier interfaz; si la interfaz tiene
el prefijo "enx", solo registra un mensaje informativo. Luego libera el skb.
Que recibe:
  - skb: trama recibida.
  - dev: interfaz por la que llegó.
  - pt: el packet_type que generó la llamada (no usado).
  - orig_dev: dispositivo original (no usado).
Que retorna: NET_RX_SUCCESS siempre (la trama se descarta tras procesarla).
Nota: esto NO es la recepción de datos del protocolo POV; es solo un log
informativo. La recepción/reensamblado real ocurre en receptor.c (espacio
de usuario, vía AF_PACKET).
*/
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

/*
Nombre de funcion: mi_driver_init
Que hace: Función de inicialización del módulo (llamada al cargarlo con
insmod/modprobe). Registra el character device (major dinámico) y
registra el hook de recepción de paquetes para CUSTOM_ETHERTYPE.
Que recibe: Nada.
Que retorna:
  - 0 si la inicialización fue exitosa.
  - El código de error de register_chrdev si falló (negativo).
*/
static int __init mi_driver_init(void)
{
    pr_info("[Mi_Driver_Proyecto] Inicializando driver...\n");

    // major=0 -> el kernel asigna un número major dinámicamente
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

/*
Nombre de funcion: mi_driver_exit
Que hace: Función de limpieza del módulo (llamada al descargarlo con
rmmod). Remueve el hook de paquetes y desregistra el character device.
Que recibe: Nada.
Que retorna: Nada (void).
*/
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

