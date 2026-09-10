#include "usb_midi_host.h"
#include "board_config.h"
#include "usb/usb_host.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_check.h"

namespace {
MidiSink sink;
usb_host_client_handle_t client;
usb_device_handle_t device;
music::Endpoint endpoint{};
usb_transfer_t *transfers[2] = {};
bool in_flight[2] = {}, closing=false, halted=false, claimed=false;
uint8_t new_address=0, retry_address=0;
int64_t retry_at=0;
unsigned debug_level=0;
void reset_music() {sink({music::Type::Reset,0,0,0,uint64_t(esp_timer_get_time())});}
void request_close() {
    if(!closing) {closing=true;reset_music();}
}
void received(usb_transfer_t *transfer) {
    unsigned index=transfer==transfers[0]?0:1;
    in_flight[index]=false;
    if(closing) return;
    if(transfer->status!=USB_TRANSFER_STATUS_COMPLETED || transfer->actual_num_bytes%4) {request_close();return;}
    for(int i=0;i<transfer->actual_num_bytes;i+=4) {
        music::Event event;
        if(music::decode_usb(transfer->data_buffer+i,uint64_t(esp_timer_get_time()),event)) {
            if(MIDI_DEBUG_ENABLED && event.type==music::Type::On) gpio_set_level(gpio_num_t(MIDI_DEBUG_PIN),debug_level^=1);
            sink(event);
        }
    }
    if(usb_host_transfer_submit(transfer)==ESP_OK) in_flight[index]=true;
    else request_close();
}
void client_event(const usb_host_client_event_msg_t *event,void *) {
    if(event->event==USB_HOST_CLIENT_EVENT_NEW_DEV) new_address=event->new_dev.address;
    if(event->event==USB_HOST_CLIENT_EVENT_DEV_GONE && event->dev_gone.dev_hdl==device) {
        retry_address=0;request_close();
    }
}
void open_device(uint8_t address) {
    if(usb_host_device_open(client,address,&device)!=ESP_OK) {device=nullptr;return;}
    const usb_config_desc_t *config=nullptr;
    if(usb_host_get_active_config_descriptor(device,&config)!=ESP_OK ||
       !music::find_endpoint(reinterpret_cast<const uint8_t *>(config),config->wTotalLength,endpoint)) {
        usb_host_device_close(client,device);device=nullptr;return;
    }
    if(usb_host_interface_claim(client,device,endpoint.interface_number,endpoint.alternate)!=ESP_OK) {
        usb_host_device_close(client,device);device=nullptr;return;
    }
    claimed=true;retry_address=address;
    for(unsigned i=0;i<2;++i) {
        if(usb_host_transfer_alloc(endpoint.packet_size,0,&transfers[i])!=ESP_OK) {request_close();return;}
        auto *t=transfers[i];t->device_handle=device;t->bEndpointAddress=endpoint.address;
        t->callback=received;t->num_bytes=endpoint.packet_size;
        if(usb_host_transfer_submit(t)!=ESP_OK) {request_close();return;}
        in_flight[i]=true;
    }
}
void host_task(void *) {
    usb_host_client_config_t config={};config.max_num_event_msg=8;
    config.async.client_event_callback=client_event;
    ESP_ERROR_CHECK(usb_host_client_register(&config,&client));
    for(;;) {
        usb_host_client_handle_events(client,pdMS_TO_TICKS(10));
        if(closing && device) {
            if(!halted) {
                usb_host_endpoint_halt(device,endpoint.address);
                usb_host_endpoint_flush(device,endpoint.address);
                halted=true;
            }
            if(!in_flight[0] && !in_flight[1]) {
                for(auto &t:transfers) if(t) {usb_host_transfer_free(t);t=nullptr;}
                if(claimed) usb_host_interface_release(client,device,endpoint.interface_number);
                usb_host_device_close(client,device);device=nullptr;
                closing=halted=claimed=false;retry_at=esp_timer_get_time()+100000;
            }
        }
        if(!device && new_address) {uint8_t a=new_address;new_address=0;open_device(a);}
        else if(!device && retry_address && esp_timer_get_time()>=retry_at) {
            uint8_t a=retry_address;retry_address=0;open_device(a);
        }
    }
}
void daemon_task(void *) {
    for(;;) {uint32_t flags;usb_host_lib_handle_events(portMAX_DELAY,&flags);}
}
}
void usb_midi_start(MidiSink callback) {
    sink=callback;
    if(MIDI_DEBUG_ENABLED) {gpio_reset_pin(gpio_num_t(MIDI_DEBUG_PIN));gpio_set_direction(gpio_num_t(MIDI_DEBUG_PIN),GPIO_MODE_OUTPUT);}
    if(USB_VBUS_ENABLE_PIN>=0) {
        gpio_set_direction(gpio_num_t(USB_VBUS_ENABLE_PIN),GPIO_MODE_OUTPUT);
        gpio_set_level(gpio_num_t(USB_VBUS_ENABLE_PIN),USB_VBUS_ENABLE_LEVEL);
    }
    usb_host_config_t config={};config.intr_flags=ESP_INTR_FLAG_LEVEL1;
    ESP_ERROR_CHECK(usb_host_install(&config));
    configASSERT(xTaskCreatePinnedToCore(daemon_task,"usb-library",4096,nullptr,22,nullptr,0)==pdPASS);
    configASSERT(xTaskCreatePinnedToCore(host_task,"usb-midi",4096,nullptr,21,nullptr,0)==pdPASS);
}
