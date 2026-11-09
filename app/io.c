#include "app.h"
#include "main.h"

#include "dhserver.h"
#include "dnserver.h"
#include "lwip/ethip6.h"
#include "lwip/init.h"
#include "lwip/sys.h"
#include "lwip/timeouts.h"
#include "tusb.h"

#include <assert.h>

size_t board_usb_get_serial(uint16_t id[], size_t max_len) {
  const uint32_t uuid[] = {
      HAL_GetUIDw0(),
      HAL_GetUIDw1(),
      HAL_GetUIDw2(),
  };
  assert(max_len > sizeof(uuid));
  memcpy(id, uuid, sizeof(uuid));
  return sizeof(uuid);
}

#define INIT_IP4(a, b, c, d) {PP_HTONL(LWIP_MAKEU32(a, b, c, d))}

/* lwip context */
static struct netif netif_data = {0};

/* this is used by this code, ./class/net/net_driver.c, and usb_descriptors.c */
/* ideally speaking, this should be generated from the hardware's unique ID (if
 * available) */
/* it is suggested that the first byte is 0x02 to indicate a link-local address
 */
uint8_t tud_network_mac_address[6] = {0x02, 0x02, 0x84, 0x6A, 0x96, 0x00};

/* network parameters of this MCU */
static const ip4_addr_t ipaddr = INIT_IP4(192, 168, 7, 1);
static const ip4_addr_t netmask = INIT_IP4(255, 255, 255, 0);
static const ip4_addr_t gateway = INIT_IP4(0, 0, 0, 0);

/* database IP addresses that can be offered to the host; this must be in RAM to
 * store assigned MAC addresses */
static dhcp_entry_t entries[] = {
    /* mac ip address               lease time */
    {{0}, INIT_IP4(192, 168, 7, 2), 24 * 60 * 60},
    {{0}, INIT_IP4(192, 168, 7, 3), 24 * 60 * 60},
    {{0}, INIT_IP4(192, 168, 7, 4), 24 * 60 * 60},
};

static const dhcp_config_t dhcp_config = {
    .router = INIT_IP4(0, 0, 0, 0),  /* router address (if any) */
    .port = 67,                      /* listen port */
    .dns = INIT_IP4(192, 168, 7, 1), /* dns server (if any) */
    "usb",                           /* dns suffix */
    TU_ARRAY_SIZE(entries),          /* num entry */
    entries                          /* entries */
};

static err_t linkoutput_fn(struct netif *netif, struct pbuf *p) {
  (void)netif;

  for (;;) {
    /* if TinyUSB isn't ready, we must signal back to lwip that there is nothing
     * we can do */
    if (!tud_ready())
      return ERR_USE;

    /* if the network driver can accept another packet, we make it happen */
    if (tud_network_can_xmit(p->tot_len)) {
      tud_network_xmit(p, 0 /* unused for this example */);
      return ERR_OK;
    }

    /* transfer execution to TinyUSB in the hopes that it will finish
     * transmitting the prior packet */
    tud_task();
  }
}

static err_t ip4_output_fn(struct netif *netif, struct pbuf *p,
                           const ip4_addr_t *addr) {
  return etharp_output(netif, p, addr);
}

#if LWIP_IPV6
static err_t ip6_output_fn(struct netif *netif, struct pbuf *p,
                           const ip6_addr_t *addr) {
  return ethip6_output(netif, p, addr);
}
#endif

static err_t netif_init_cb(struct netif *netif) {
  LWIP_ASSERT("netif != NULL", (netif != NULL));
  netif->mtu = CFG_TUD_NET_MTU;
  netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_LINK_UP |
                 NETIF_FLAG_UP;
  netif->state = NULL;
  netif->name[0] = 'E';
  netif->name[1] = 'X';
  netif->linkoutput = linkoutput_fn;
  netif->output = ip4_output_fn;
#if LWIP_IPV6
  netif->output_ip6 = ip6_output_fn;
#endif
  return ERR_OK;
}

/* notifies the USB host about the link state change. */
static void usbnet_netif_link_callback(struct netif *netif) {
  bool link_up = netif_is_link_up(netif);
  tud_network_link_state(BOARD_TUD_RHPORT, link_up);
}

static void init_lwip(void) {
  struct netif *netif = &netif_data;

  lwip_init();

  /* the lwip virtual MAC address must be different from the host's; to ensure
   * this, we toggle the LSbit */
  netif->hwaddr_len = sizeof(tud_network_mac_address);
  memcpy(netif->hwaddr, tud_network_mac_address,
         sizeof(tud_network_mac_address));
  netif->hwaddr[5] ^= 0x01;

  netif = netif_add(netif, &ipaddr, &netmask, &gateway, NULL, netif_init_cb,
                    ethernet_input);
#if LWIP_IPV6
  netif_create_ip6_linklocal_address(netif, 1);
#endif
  netif_set_default(netif);

#if LWIP_NETIF_LINK_CALLBACK
  // Set the link callback to notify USB host about link state changes
  netif_set_link_callback(netif, usbnet_netif_link_callback);
  netif_set_link_up(netif);
#else
  tud_network_link_state(BOARD_TUD_RHPORT, true);
#endif
}

/* handle any DNS requests from dns-server */
static bool dns_query_proc(const char *name, ip4_addr_t *addr) {
  if (0 == strcmp(name, "tiny.usb")) {
    *addr = ipaddr;
    return true;
  }
  return false;
}

bool tud_network_recv_cb(const uint8_t *src, uint16_t size) {
  struct netif *netif = &netif_data;

  if (size) {
    struct pbuf *p = pbuf_alloc(PBUF_RAW, size, PBUF_POOL);

    if (p == NULL) {
      printf("ERROR: Failed to allocate pbuf of size %d\n", size);
      return false;
    }

    /* Copy buf to pbuf */
    pbuf_take(p, src, size);

    // Surrender ownership of our pbuf unless there was an error
    // Only call pbuf_free if not Ok else it will panic with "pbuf_free: p->ref
    // > 0" or steal it from whatever took ownership of it with undefined
    // consequences. See: https://savannah.nongnu.org/patch/index.php?10121
    if (netif->input(p, netif) != ERR_OK) {
      printf("ERROR: netif input failed\n");
      pbuf_free(p);
    }
    // Signal tinyusb that the current frame has been processed.
    tud_network_recv_renew();
  }

  return true;
}

uint16_t tud_network_xmit_cb(uint8_t *dst, void *ref, uint16_t arg) {
  struct pbuf *p = (struct pbuf *)ref;

  (void)arg; /* unused for this example */

  return pbuf_copy_partial(p, dst, p->tot_len, 0);
}

sys_prot_t sys_arch_protect(void) { return 0; }

void sys_arch_unprotect(sys_prot_t pval) { (void)pval; }

uint32_t sys_now(void) { return HAL_GetTick(); }

EventTaskSet_t link_state_listeners;

static bool is_link_up = false;

bool is_link_state_up() { return is_link_up; }

// Invoked when device is mounted
void tud_mount_cb(void) {
  is_link_up = true;
  invoke_tasks(&link_state_listeners);
}

// Invoked when device is unmounted
void tud_umount_cb(void) {
  is_link_up = false;
  invoke_tasks(&link_state_listeners);
}

// Invoked when usb bus is suspended
// remote_wakeup_en : if host allow us to perform remote wakeup
// Within 7ms, device must draw an average of current less than 2.5 mA from bus
void tud_suspend_cb(bool remote_wakeup_en) {
  (void)remote_wakeup_en;
  is_link_up = false;
  invoke_tasks(&link_state_listeners);
}

// Invoked when usb bus is resumed
void tud_resume_cb(void) {
  is_link_up = true;
  invoke_tasks(&link_state_listeners);
}

void handle_otg_irq() {
  tusb_int_handler(BOARD_TUD_RHPORT, true);
  osEventFlagsSet(defaultTaskEventHandle, APP_EVENT_USB);
}

void init_io() {
  init(&link_state_listeners);
  /* initialize TinyUSB */

  // init device stack on configured roothub port
  tusb_rhport_init_t dev_init = {.role = TUSB_ROLE_DEVICE,
                                 .speed = TUSB_SPEED_AUTO};
  tusb_init(BOARD_TUD_RHPORT, &dev_init);

  /* initialize lwip, dhcp-server, dns-server, and http */
  init_lwip();
  while (!netif_is_up(&netif_data))
    ;
  while (dhserv_init(&dhcp_config) != ERR_OK)
    ;
  while (dnserv_init(IP_ADDR_ANY, 53, dns_query_proc) != ERR_OK)
    ;
}

extern PCD_HandleTypeDef hpcd_USB_OTG_FS;
static void MX_USB_OTG_FS_PCD_Deinit(void) {
  if (HAL_PCD_DeInit(&hpcd_USB_OTG_FS) != HAL_OK) {
    Error_Handler();
  }
}

/**
 * @brief GPIO Initialization Function
 * @param None
 * @retval None
 */
static void MX_GPIO_Deinit(void) {
  /* EXTI interrupt deinit*/
  HAL_NVIC_DisableIRQ(K1_EXTI_IRQn);

  /*Configure GPIO pin : K1_Pin */
  HAL_GPIO_DeInit(K1_GPIO_Port, K1_Pin);

  /*Configure GPIO pin : LED_Pin */
  HAL_GPIO_DeInit(LED_GPIO_Port, LED_Pin);

  __HAL_RCC_GPIOE_CLK_DISABLE();
  __HAL_RCC_GPIOH_CLK_DISABLE();
  __HAL_RCC_GPIOA_CLK_DISABLE();
}

void deinit_io() {
  tusb_deinit(BOARD_TUD_RHPORT);
  cleanup(&link_state_listeners);

  MX_USB_OTG_FS_PCD_Deinit();
  MX_GPIO_Deinit();
}
