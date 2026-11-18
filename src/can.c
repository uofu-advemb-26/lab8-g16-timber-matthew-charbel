#include <FreeRTOS.h>
#include <queue.h>
#include <task.h>
#include <pico/cyw43_arch.h>

#include <can2040.h>
#include <hardware/regs/intctrl.h>
#include <stdio.h>
#include <pico/stdlib.h>

// Activity 2 configuration
// BABBLER = 1 : this board is the babbling node (high priority, sends nonstop)
// BABBLER = 0 : this board sends a low-priority message periodically
#ifndef BABBLER
#define BABBLER 1
#endif

// Busy-wait inserted after each babbler frame. Start at 0 and increase until the low-priority
// messages are consistently received.
#ifndef BABBLE_DELAY_US
#define BABBLE_DELAY_US 0
#endif

#define HIGH_PRIORITY_ID 0x010   // lower ID wins arbitration
#define LOW_PRIORITY_ID  0x200
#define LOW_PRIORITY_PERIOD_MS 100

#define STATS_PERIOD_MS 1000

static struct can2040 cbus;
static int count = 0;

QueueHandle_t receiveQueue;

// Counters updated from the CAN IRQ; read (never reset) by the receiver task.
static volatile uint32_t txCompleted = 0;   // frames this board actually put on the bus
static volatile uint32_t rxDropped = 0;     // frames lost because receiveQueue was full
static volatile uint32_t txRejected = 0;    // can2040_transmit calls that failed (tx queue full)

static void can2040_cb(struct can2040 *cd, uint32_t notify, struct can2040_msg *msg)
{
    if (notify == CAN2040_NOTIFY_TX) {
        txCompleted++;
        return;
    }
    if (notify != CAN2040_NOTIFY_RX) return;

    // Called from the PIO IRQ, so the ISR-safe queue API must be used.
    BaseType_t higherPriorityTaskWoken = pdFALSE;
    if (xQueueSendToBackFromISR(receiveQueue, msg, &higherPriorityTaskWoken) != pdPASS) {
        rxDropped++;
    }
    portYIELD_FROM_ISR(higherPriorityTaskWoken);
}

static void PIOx_IRQHandler(void)
{
    can2040_pio_irq_handler(&cbus);
}

static int canSend(uint32_t id){
    struct can2040_msg msg;
    msg.id = id;
    msg.dlc = 8;

    for (int i = 0; i < 8; i++) {
        msg.data[i] = '0' + count;
    }

    count++;
    count %= 10;
    return can2040_transmit(&cbus, &msg);
}


void canbus_setup(void)
{
    uint32_t pio_num = 0;
    uint32_t sys_clock = 125000000, bitrate = 500000;
    uint32_t gpio_rx = 4, gpio_tx = 5;

    // Setup canbus
    can2040_setup(&cbus, pio_num);
    can2040_callback_config(&cbus, can2040_cb);

    // Enable irqs
    irq_set_exclusive_handler(PIO0_IRQ_0, PIOx_IRQHandler);
    irq_set_priority(PIO0_IRQ_0, PICO_DEFAULT_IRQ_PRIORITY - 1);
    irq_set_enabled(PIO0_IRQ_0, 1);

    // Start canbus
    can2040_start(&cbus, sys_clock, bitrate, gpio_rx, gpio_tx);
}


void sender_task(__unused void *params)
{
#if BABBLER
    // Always keep a high-priority frame queued so it is ready
    // to win arbitration every time the bus goes idle.
    while(true){
        while (!can2040_check_transmit(&cbus)) {
            // spin until can2040's transmit queue has room
        }
        if (canSend(HIGH_PRIORITY_ID) < 0) txRejected++;
        if (BABBLE_DELAY_US > 0) busy_wait_us(BABBLE_DELAY_US);
    }
#else
    // Well-behaved node: low-priority frame every LOW_PRIORITY_PERIOD_MS.
    while(true){
        vTaskDelay(pdMS_TO_TICKS(LOW_PRIORITY_PERIOD_MS));
        if (canSend(LOW_PRIORITY_ID) < 0) txRejected++;
    }
#endif
}

void receiver_task(__unused void *params)
{
    struct can2040_msg msg;
    uint32_t rxHigh = 0, rxLow = 0, rxOther = 0;
    uint32_t lastHigh = 0, lastLow = 0, lastOther = 0;
    uint32_t lastTx = 0, lastDropped = 0, lastRejected = 0;
    TickType_t nextPrint = xTaskGetTickCount() + pdMS_TO_TICKS(STATS_PERIOD_MS);

    printf("CAN activity 2: %s, babble delay %d us\n",
           BABBLER ? "BABBLER (id 0x010)" : "PERIODIC (id 0x200)", BABBLE_DELAY_US);

    while(true){
        // Wait for a frame, but wake up in time to print the stats.
        TickType_t now = xTaskGetTickCount();
        TickType_t wait = (nextPrint > now) ? (nextPrint - now) : 0;

        if (xQueueReceive(receiveQueue, &msg, wait) == pdPASS) {
            if (msg.id == HIGH_PRIORITY_ID)      rxHigh++;
            else if (msg.id == LOW_PRIORITY_ID)  rxLow++;
            else                                 rxOther++;
        }

        if (xTaskGetTickCount() >= nextPrint) {
            uint32_t tx = txCompleted, dropped = rxDropped, rejected = txRejected;
            printf("rx/s: 0x010=%lu 0x200=%lu other=%lu | tx/s=%lu tx_rejected/s=%lu rx_dropped/s=%lu\n",
                   (unsigned long)(rxHigh - lastHigh),
                   (unsigned long)(rxLow - lastLow),
                   (unsigned long)(rxOther - lastOther),
                   (unsigned long)(tx - lastTx),
                   (unsigned long)(rejected - lastRejected),
                   (unsigned long)(dropped - lastDropped));
            lastHigh = rxHigh; lastLow = rxLow; lastOther = rxOther;
            lastTx = tx; lastDropped = dropped; lastRejected = rejected;
            nextPrint += pdMS_TO_TICKS(STATS_PERIOD_MS);
        }
    }
}

int main(void)
{
    receiveQueue = xQueueCreate(100, sizeof(struct can2040_msg));
    stdio_init_all();
    canbus_setup();
    TaskHandle_t sendTask;
    TaskHandle_t receiveTask;

    // Receiver runs at a higher priority so it still gets CPU time while the
    // babbler's sender task spins without blocking.
    xTaskCreate(sender_task, "SenderThread",
                configMINIMAL_STACK_SIZE, NULL, tskIDLE_PRIORITY + 1UL, &sendTask);
    xTaskCreate(receiver_task, "ReceiverThread",
                configMINIMAL_STACK_SIZE * 4, NULL, tskIDLE_PRIORITY + 2UL, &receiveTask);
    vTaskStartScheduler();
    return 0;
}
