#include "kb17.h"

#include "hal.h"
#include "hal_low_power_lld.h"

/* Real CPU low power while the USB bus is suspended.
 * suspend_power_down_kb() is invoked on every iteration of the suspend loop
 * in protocol_pre_task() (tmk_core/protocol/chibios/chibios.c) while the bus
 * stays suspended.  We override the weak default here to enter the SK32 STOP
 * mode (deep sleep) instead of the plain wait_ms(17) busy loop: the core halts
 * until an interrupt wakes it (the forced USB RESUME kept armed by the USB
 * low level driver, or an EXTI).  On a confirmed wakeup suspend_wakeup_init_kb()
 * rebuilds the system/PLL clocks torn down by STOP.
 * Gated on SK32_HAL_USE_LOWPOWER so it only affects this board. */
#if (SK32_HAL_USE_LOWPOWER == TRUE)

/* KB17 matrix: 5 rows x 4 cols, DIODE_DIRECTION ROW2COL (see config.h).
 * The QMK matrix scan drives a column low (select_col) and reads the rows as
 * inputs with pull-up.  While the core sleeps we mirror that electrical state:
 * every row is an input with pull-up armed on the EXTI falling edge, and every
 * column is held as a push-pull output LOW.  Pressing any key shorts its row
 * to the (low) column, pulling the row down -> falling edge -> STOP exit. */
static const ioline_t kb17_wake_rows[] = {
    PAL_LINE(GPIOB, 2),  /* ROW1 (PB2) */
    PAL_LINE(GPIOB, 1),  /* ROW2 (PB1) */
    PAL_LINE(GPIOB, 0),  /* ROW3 (PB0) */
    PAL_LINE(GPIOC, 5),  /* ROW4 (PC5) */
    PAL_LINE(GPIOC, 4),  /* ROW5 (PC4) */
};

static const ioline_t kb17_wake_cols[] = {
    PAL_LINE(GPIOB, 12), /* COL1 (PB12) */
    PAL_LINE(GPIOB, 11), /* COL2 (PB11) */
    PAL_LINE(GPIOB, 10), /* COL3 (PB10) */
    PAL_LINE(GPIOB, 13), /* COL4 (PB13) */
};

#define KB17_WAKE_ROW_COUNT (sizeof(kb17_wake_rows) / sizeof(kb17_wake_rows[0]))
#define KB17_WAKE_COL_COUNT (sizeof(kb17_wake_cols) / sizeof(kb17_wake_cols[0]))

static uint32_t kb17_wake_row_mask(void) {
    uint32_t mask = 0U;

    for (unsigned i = 0U; i < KB17_WAKE_ROW_COUNT; i++) {
        mask |= 1U << (uint32_t)PAL_PAD(kb17_wake_rows[i]);
    }

    return mask;
}

/* Arms every row as a STOP wake source and holds every column LOW.
 * Reference: SK32F0xx_Firmware Package 26_07_21, RGBKeyboardSTK
 * (Mechanical_SRGB), User/Src/kbcuDrive.c -> KBCUDVE_SleepInit(): before
 * PWR_EnterSTOPMode() the vendor switches each key to input with pull-up and
 * arms its EXTI line on the falling edge so the first key press pulls the core
 * out of deep sleep.  For a ROW2COL matrix the equivalent is to keep all
 * columns selected (output LOW) so any key press pulls its row down. */
static void kb17_stop_wake_pins_arm(void) {
    uint32_t row_mask = kb17_wake_row_mask();

    /* The EXTI line -> port mapping lives in SYSCFG, which needs its APB2
       clock (the vendor calls RCC_APB2PeriphClockCmd(..SYSCFG, ENABLE) in
       KBCUDVE_SleepInit() for the same reason). */
    rccEnableAPB2(RCC_APB2ENR_SYSCFGEN, true);

    for (unsigned i = 0U; i < KB17_WAKE_ROW_COUNT; i++) {
        ioline_t line  = kb17_wake_rows[i];
        uint32_t pad   = (uint32_t)PAL_PAD(line);
        uint32_t port  = (((uint32_t)PAL_PORT(line) - (uint32_t)GPIOA) >> 10U) & 0xFU;
        uint32_t cr    = pad >> 2U;
        uint32_t shift = (pad & 3U) * 4U;

        /* Input with pull-up: the key shorts the row to a driven-low column. */
        palSetLineMode(line, PAL_MODE_INPUT_PULLUP);

        /* Route the line to its port, 4 bits per line in SYSCFG EXTICR. */
        SYSCFG->EXTICR[cr] = (SYSCFG->EXTICR[cr] & ~(0xFU << shift)) | (port << shift);
    }

    /* Columns driven LOW (all "selected") so a press on any key pulls a row. */
    for (unsigned i = 0U; i < KB17_WAKE_COL_COUNT; i++) {
        palSetLineMode(kb17_wake_cols[i], PAL_MODE_OUTPUT_PUSHPULL);
        palClearLine(kb17_wake_cols[i]);
    }

    /* Falling edge on the rows: pressing a key pulls the row low.  The event
       mask is set next to the interrupt mask, exactly like USBUSER_Init() arms
       the USB resume line: the wakeup out of STOP is fed by the EXTI request,
       and none of these lines has an NVIC vector of its own on this family. */
    EXTI->RTSR &= ~row_mask;
    EXTI->FTSR |= row_mask;
    EXTI->EMR  |= row_mask;
    EXTI->IMR  |= row_mask;

    /* Drop anything latched before the arming (a key already held down would
       otherwise end the first WFE immediately with a stale edge). */
    EXTI->PR = row_mask;
}

/* Releases the wake configuration once the core is awake again: outside the
 * suspend path QMK owns the pins as plain matrix inputs/outputs. */
static void kb17_stop_wake_pins_release(void) {
    uint32_t row_mask = kb17_wake_row_mask();

    EXTI->IMR  &= ~row_mask;
    EXTI->EMR  &= ~row_mask;
    EXTI->FTSR &= ~row_mask;
    EXTI->RTSR &= ~row_mask;
    EXTI->PR    = row_mask;

    /* Restore the columns to the QMK matrix unselected state (input with
       pull-up, which is what unselect_col() puts them in without
       MATRIX_UNSELECT_DRIVE_HIGH) so the next matrix scan starts clean. */
    for (unsigned i = 0U; i < KB17_WAKE_COL_COUNT; i++) {
        palSetLineMode(kb17_wake_cols[i], PAL_MODE_INPUT_PULLUP);
    }
}

void suspend_power_down_kb(void) {
    /* Arm the keys as wake sources, sleep, then release them so the matrix
       scan in suspend_wakeup_condition() reads the pins as plain inputs. */
    kb17_stop_wake_pins_arm();
    sk32_lowpower_stop_enter();

    /* Which rows latched a falling edge while the core slept: the same
       hardware evidence the vendor uses to leave STOP.  Read before the
       release below clears EXTI->PR. */
    uint32_t wake_rows = EXTI->PR & kb17_wake_row_mask();

    sk32_lowpower_stop_restore();
    kb17_stop_wake_pins_release();

    /* Remote wakeup (device -> host).
     * Mirrors the vendor usbd_set_remote_wakeup() (SK32F0xx_Firmware Package
     * 25_10_24, RGBKeyboardSTK (Mechanical)): on a key edge the device drives
     * USB resume unconditionally.
     *
     * QMK's own call site in protocol_pre_task() gates this on
     * (USB_DRIVER.status & USB_GETSTATUS_REMOTE_WAKEUP_ENABLED), i.e. on the
     * host having sent SET_FEATURE(DEVICE_REMOTE_WAKEUP).  This host never
     * sends it (USBD1.status stays 0, verified over SWD on the LD7), so that
     * path would never signal the bus and a key press could not wake the PC.
     * Calling usbWakeupHost() here bypasses that gate; it still only acts
     * while state == USB_SUSPENDED, so the bus is only driven when it is
     * really suspended. */
    if (wake_rows != 0U) {
        usbWakeupHost(&USBD1);
    }

    suspend_power_down_user();
}

void suspend_wakeup_init_kb(void) {
    /* STOP dropped the PLL/HSI clock config; restore it before any further
       USB/report processing.  Handled here (after the loop has determined a
       real wakeup) so an unrelated interrupt that merely wakes STOP does not
       force a full clock rebuild. */
    sk32_lowpower_stop_restore();
    suspend_wakeup_init_user();
}
#endif /* SK32_HAL_USE_LOWPOWER */


#ifdef RGB_MATRIX_ENABLE

led_config_t g_led_config = {
	{
		{0, 1, 2, 3},
		{4, 5, 6, NO_LED},
		{7, 8, 9, 10},
		{11, 12, 13, NO_LED},
		{14, NO_LED, 15, 16},
	}, {
		{28, 3}, {84, 3}, {140, 3}, {196, 3},
		{28, 16}, {84, 16}, {140, 16},
		{28, 29}, {84, 29}, {140, 29}, {196, 19},
		{28, 41}, {84, 41}, {140, 41},
		{56, 54}, {140, 54}, {196, 45},
	}, {
		4, 4, 4, 4,
		4, 4, 4,
		4, 4, 4, 4,
		4, 4, 4,
		4, 4, 4,
	}
};

#endif
