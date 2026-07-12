#include <devices/ps2mouse.h>
#include <graphics/basic.h>
#include <scheduler.h>

static constexpr ui16 ps2_data_port   = 0x60;
static constexpr ui16 ps2_status_port = 0x64;
static constexpr ui32 ps2_timeout     = 100000;

static ps2_mouse_state_t mouse_state = {};
static ui8               mouse_packet[3];
static ui8               mouse_packet_index;

static inline ui8 inb(ui16 port) {
    ui8 value;
    asm("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void outb(ui16 port, ui8 value) { asm("outb %0, %1" : : "a"(value), "Nd"(port)); }

static bool wait_to_write() {
    for (ui32 attempt = 0; attempt < ps2_timeout; attempt++) {
        if (!(inb(ps2_status_port) & 0x02))
            return true;
    }
    return false;
}

static bool wait_to_read(bool auxiliary) {
    for (ui32 attempt = 0; attempt < ps2_timeout; attempt++) {
        ui8 status = inb(ps2_status_port);
        if ((status & 0x01) && static_cast<bool>(status & 0x20) == auxiliary)
            return true;
    }
    return false;
}

static bool write_controller(ui8 value) {
    if (!wait_to_write())
        return false;
    outb(ps2_status_port, value);
    return true;
}

static bool write_data(ui8 value) {
    if (!wait_to_write())
        return false;
    outb(ps2_data_port, value);
    return true;
}

static bool write_mouse(ui8 value) {
    if (!write_controller(0xD4) || !write_data(value) || !wait_to_read(true))
        return false;
    return inb(ps2_data_port) == 0xFA;
}

bool ps2mouse_init() {
    if (!write_controller(0xA8) || !write_controller(0x20) || !wait_to_read(false))
        return false;

    ui8 configuration = inb(ps2_data_port);
    configuration     = (configuration | 0x03) & static_cast<ui8>(~0x20);
    if (!write_controller(0x60) || !write_data(configuration))
        return false;

    if (!write_mouse(0xF6) || !write_mouse(0xF4))
        return false;
    graphics_move_cursor(0, 0);
    return true;
}

extern "C" void ps2mouse_interrupt() {
    ui8 value = inb(ps2_data_port);
    outb(0xa0, 0x20);
    outb(0x20, 0x20);
    if (mouse_packet_index == 0 && !(value & 0x08))
        return;

    mouse_packet[mouse_packet_index++] = value;
    if (mouse_packet_index != 3)
        return;

    mouse_packet_index = 0;
    if (mouse_packet[0] & 0xC0)
        return;

    i8 delta_x = static_cast<i8>(mouse_packet[1]);
    i8 delta_y = static_cast<i8>(mouse_packet[2]);
    mouse_state.x += delta_x;
    mouse_state.y += delta_y;
    mouse_state.left_button   = mouse_packet[0] & 0x01;
    mouse_state.right_button  = mouse_packet[0] & 0x02;
    mouse_state.middle_button = mouse_packet[0] & 0x04;
    mouse_state.packet_count++;
    graphics_move_cursor(delta_x, -delta_y);
}

ps2_mouse_state_t ps2mouse_get_state() { return mouse_state; }

void ps2mouse_main(void *arg) {
    (void)arg;
    ui8 packet[3];
    ui8 packet_index = 0;

    while (true) {
        ui8 status = inb(ps2_status_port);
        if ((status & 0x21) == 0x21) {
            ui8 value = inb(ps2_data_port);
            if (packet_index == 0 && !(value & 0x08))
                continue;

            packet[packet_index++] = value;
            if (packet_index == 3) {
                packet_index = 0;
                if (packet[0] & 0xC0)
                    continue;

                i8 delta_x = static_cast<i8>(packet[1]);
                i8 delta_y = static_cast<i8>(packet[2]);
                mouse_state.x += delta_x;
                mouse_state.y += delta_y;
                mouse_state.left_button   = packet[0] & 0x01;
                mouse_state.right_button  = packet[0] & 0x02;
                mouse_state.middle_button = packet[0] & 0x04;
                mouse_state.packet_count++;
                graphics_move_cursor(delta_x, -delta_y);
            }
        }
        scheduler_yield();
    }
}
