#include "music.h"
#include "usb_midi_host.h"
#include "board_config.h"
#include "note_set_wire.h"
#include "driver/uart.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <cstring>

// Inspect using debugger; no printf in the event path. These are ESP-local times,
// not end-to-end latency. Use both GPIOs and a logic analyser for that.
struct Timing { uint64_t count=0,total_us=0,min_us=UINT64_MAX,max_us=0; };
volatile Timing music_processing, receive_to_processed;
volatile uint32_t midi_queue_overflows=0, note_set_coalesced=0;
static QueueHandle_t events;
static music::Engine engine;
static void record(volatile Timing &s,uint64_t us) {
    s.count=s.count+1;s.total_us=s.total_us+us;if(us<s.min_us)s.min_us=us;if(us>s.max_us)s.max_us=us;
}
static void incoming(const music::Event &event) {
    if(event.type==music::Type::Reset) xQueueReset(events);
    if(xQueueSend(events,&event,0)!=pdTRUE) {
        midi_queue_overflows=midi_queue_overflows+1;xQueueReset(events);
        music::Event reset{music::Type::Reset,0,0,0,event.timestamp};
        xQueueSend(events,&reset,0); // Explicit fail-silent recovery; no stuck state.
    }
}
static void musical_task(void *) {
    uint8_t frame[NS_SIZE]={}, sequence=0;
    unsigned offset=NS_SIZE;
    bool pending=true;
    music::NoteSet current;
    int64_t last_sent=0;
    for(;;) {
        music::Event event;
        TickType_t wait=(pending || offset<NS_SIZE)?1:pdMS_TO_TICKS(NS_HEARTBEAT_MS);
        if(xQueueReceive(events,&event,wait)==pdTRUE) {
            uint64_t begin=esp_timer_get_time();
            auto next=engine.process(event);
            uint64_t end=esp_timer_get_time();
            record(music_processing,end-begin);
            record(receive_to_processed,end>=event.timestamp?end-event.timestamp:0);
            if(!(next==current) || event.type==music::Type::Reset) {
                if(pending) note_set_coalesced=note_set_coalesced+1;
                current=next;pending=true;
            }
        }
        int64_t now=esp_timer_get_time();
        if(now-last_sent>=NS_HEARTBEAT_MS*1000) pending=true;
        // Only coalesce snapshots that have not started. Never splice UART frames.
        if(offset==NS_SIZE && pending) {
            ns_encode(frame,sequence++,current.notes,current.count);
            offset=0;pending=false;
        }
        if(offset<NS_SIZE) {
            int written=uart_tx_chars(UART_NUM_1,reinterpret_cast<const char *>(frame+offset),NS_SIZE-offset);
            if(written>0) offset+=unsigned(written);
            if(offset==NS_SIZE) last_sent=now;
        }
    }
}
extern "C" void app_main(void) {
    uart_config_t uart={};uart.baud_rate=NS_BAUD;uart.data_bits=UART_DATA_8_BITS;
    uart.parity=UART_PARITY_DISABLE;uart.stop_bits=UART_STOP_BITS_1;
    uart.flow_ctrl=UART_HW_FLOWCTRL_DISABLE;uart.source_clk=UART_SCLK_DEFAULT;
    ESP_ERROR_CHECK(uart_param_config(UART_NUM_1,&uart));
    ESP_ERROR_CHECK(uart_set_pin(UART_NUM_1,MIDI_UART_TX,MIDI_UART_RX,UART_PIN_NO_CHANGE,UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM_1,256,0,0,nullptr,0));
    events=xQueueCreate(MIDI_EVENT_QUEUE_SIZE,sizeof(music::Event));configASSERT(events);
    configASSERT(xTaskCreatePinnedToCore(musical_task,"music",16384,nullptr,20,nullptr,1)==pdPASS);
    usb_midi_start(incoming);
}
