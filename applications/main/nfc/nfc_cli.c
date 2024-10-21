#include <furi.h>
#include <furi_hal.h>
#include <furi_hal_nfc.h>
#include <furi_hal_nfc_i.h>

#include <cli/cli.h>
#include <lib/toolbox/args.h>
#include <lib/toolbox/hex.h>

#include <lib/nfc/helpers/iso14443_crc.h>
#include <nfc/helpers/iso13239_crc.h>

#include <toolbox/bit_buffer.h>
#include <nfc/protocols/iso15693_3/iso15693_3.h>
#include <nfc/protocols/iso14443_3a/iso14443_3a.h>
//#define NFC_CLI_VERBOSE

FuriHalNfcTech g_NfcTech = FuriHalNfcTechInvalid;
FuriHalNfcMode g_NfcMode = FuriHalNfcModePoller;
bool field_on = false;
bool g_nfc_low_power_mode_off = false;
bool g_hex_mode = false;
uint8_t g_uid[20] = {0xB9, 0xCC, 0x13, 0x71, 0x6F, 0x73, 0x81};
uint8_t g_uid_len = 0x04;
uint8_t g_atqa[2] = {0x04, 0x00};
uint8_t g_sak = 0x20;

static void nfc_low_power_mode_stop() {
    if(g_nfc_low_power_mode_off) {
        return;
    }
    furi_hal_nfc_low_power_mode_stop();
    g_nfc_low_power_mode_off = true;
}

static void nfc_low_power_mode_start() {
    if(!g_nfc_low_power_mode_off) {
        return;
    }
    furi_hal_nfc_low_power_mode_start();
    g_nfc_low_power_mode_off = false;
}

static void unhexify(uint8_t* bytes_in, uint8_t* bytes_out, size_t bytes_in_count) {
    //    size_t i;

    for(size_t i = 0; i < bytes_in_count / 2; i++) {
        args_char_to_hex(bytes_in[i * 2], bytes_in[i * 2 + 1], &(bytes_out[i]));
        //        if(!args_char_to_hex(bytes_in[i * 2], bytes_in[i * 2 + 1], &(bytes_out[i]))) {
        //            result = false;
        //            break;
        //        }
    }
}

static void nfc_cli_print_usage(void) {
    printf("Usage:\r\n");
    printf("nfc <cmd>\r\n");
    printf("Cmd list:\r\n");
    if(furi_hal_rtc_is_flag_set(FuriHalRtcFlagDebug)) {
        printf("\tfield\t - turn field on\r\n");
        printf("\tfield\t - turn field off \r\n");
        printf("\tmode_14443_a\t - set mode ISO 14443 A\r\n");
        printf("\tmode_14443_b\t - set mode ISO 14443 B\r\n");
        printf("\tmode_15693\t - set mode ISO 15693\r\n");
        printf("\treqa\t - perform REQA in ISO 14443 A mode\r\n");
        printf("\tsend <add_crc:in> <cmd:he>\t - send command\r\n");
    }
}

static void nfc_set_uid(FuriString* args) {
    FuriString* para;

    para = furi_string_alloc();
    if(!args_read_string_and_trim(args, para)) {
        printf("Error. No uid found!!!\r\n");
        return;
    }

    g_uid_len = (uint8_t)args_length(para) / 2;

    if(!args_read_hex_bytes(para, g_uid, g_uid_len)) {
        printf("Error. Command hex byte conversion error\r\n");
        return;
    }
}

static void nfc_set_atqa(FuriString* args) {
    FuriString* para;
    uint8_t len;

    para = furi_string_alloc();
    if(!args_read_string_and_trim(args, para)) {
        printf("Error. No uid found!!!\r\n");
        return;
    }

    len = (uint8_t)args_length(para) / 2;

    if(len != 2) {
        printf("Error. atqa length must 2 bytes.\r\n");
    }

    if(!args_read_hex_bytes(para, g_atqa, 2)) {
        printf("Error. Command hex byte conversion error\r\n");
        return;
    }
}

static void nfc_set_sak(FuriString* args) {
    int sak;

    if(!args_read_int_and_trim(args, &sak) || (sak > 256)) {
        printf("Error. Incorrect or missing sak value, value lower than 256\r\n");
        return;
    }

    g_sak = sak;
}

static void nfc_cli_field(Cli* cli, FuriString* args) {
    UNUSED(args);
    // Check if nfc worker is not busy
    if(furi_hal_nfc_is_hal_ready() != FuriHalNfcErrorNone) {
        printf("NFC chip failed to start\r\n");
        return;
    }

    furi_hal_nfc_acquire();
    nfc_low_power_mode_stop();
    furi_hal_nfc_poller_field_on();

    printf("Field is on. Don't leave device in this mode for too long!!!\r\n");
    printf("Press Ctrl+C to abort\r\n");

    while(!cli_cmd_interrupt_received(cli)) {
        furi_delay_ms(50);
    }

    nfc_low_power_mode_start();
    furi_hal_nfc_release();
}

static void nfc_cli_field_off(Cli* cli, FuriString* args) {
    UNUSED(cli);
    UNUSED(args);
    // Check if nfc worker is not busy
    if(furi_hal_nfc_is_hal_ready() != FuriHalNfcErrorNone) {
        printf("NFC chip failed to start\r\n");
        return;
    }

    furi_hal_nfc_acquire();
    nfc_low_power_mode_start();

    printf("Field is off.\r\n");
    field_on = false;

    furi_hal_nfc_release();
}

static void nfc_cli_field_on(Cli* cli, FuriString* args) {
    UNUSED(cli);
    UNUSED(args);
    // Check if nfc worker is not busy
    if(furi_hal_nfc_is_hal_ready() != FuriHalNfcErrorNone) {
        printf("Error. NFC chip failed to start\r\n");
        return;
    }

    furi_hal_nfc_acquire();
    nfc_low_power_mode_stop();
    furi_hal_nfc_poller_field_on();

    field_on = true;

    furi_hal_nfc_release();
}

static void nfc_cli_set_tech(FuriHalNfcMode NfcMode, FuriHalNfcTech NfcTech) {
    // Check if nfc worker is not busy
    if(furi_hal_nfc_is_hal_ready() != FuriHalNfcErrorNone) {
        printf("Error. NFC chip failed to start\r\n");
        return;
    }

    furi_hal_nfc_acquire();
    if(g_NfcTech != FuriHalNfcTechInvalid) {
        furi_hal_nfc_reset_mode();
    }
    if(NfcMode == FuriHalNfcModePoller) {
        furi_hal_nfc_set_mode(NfcMode, NfcTech);
    }
    furi_hal_nfc_release();

    switch(NfcTech) {
    case FuriHalNfcTechIso14443a:
        printf("Set mode ISO 14443 A\r\n");
        break;
    case FuriHalNfcTechIso14443b:
        printf("Set mode ISO 14443 B\r\n");
        break;
    case FuriHalNfcTechIso15693:
        printf("Set mode ISO 15693\r\n");
        break;
    default:
        break;
    }

    g_NfcTech = NfcTech;
    g_NfcMode = NfcMode;
}

static void nfc_cli_reqa(Cli* cli, FuriString* args) {
    UNUSED(cli);
    UNUSED(args);

    FuriHalNfcError error = FuriHalNfcErrorNone;
    FuriHalNfcEvent event;
    size_t rx_bits = 0xAA;
    uint8_t resp[256];
    uint32_t timeout_ms = 1000;

    if(!field_on) {
        printf("\tError. NFC Field not activated.\r\n");
        return;
    }

    // Check if nfc worker is not busy
    if(furi_hal_nfc_is_hal_ready() != FuriHalNfcErrorNone) {
        printf("Error. NFC chip failed to start\r\n");
        return;
    }

    furi_hal_nfc_acquire();
    furi_hal_nfc_trx_reset();

    error = furi_hal_nfc_iso14443a_poller_trx_short_frame(FuriHalNfcaShortFrameAllReq);
    if(error != FuriHalNfcErrorNone) {
        printf("Error. REQA error: %d\r\n", error);
        return;
    }

    while(timeout_ms > 0) {
        event = furi_hal_nfc_wait_event_common(10);
#ifdef NFC_CLI_VERBOSE
        printf("\t->wait 0x%X\n", event);
#endif // NFC_CLI_VERBOSE
        timeout_ms -= 10;
        if(event & FuriHalNfcEventRxEnd) {
            break;
        }
    }

    if(timeout_ms != 0) {
        error = furi_hal_nfc_poller_rx(resp, 10, &rx_bits);
#ifdef NFC_CLI_VERBOSE
        printf("reqa resp. code %d - length %d\r\n", error, rx_bits);
#endif // NFC_CLI_VERBOSE
        printf("%02X%02X\r\n", resp[0], resp[1]);
    } else {
        printf("Error. Timeout\r\n");
    }
    furi_hal_nfc_release();
}

static void nfc_send(BitBuffer* cmd, bool add_crc, uint32_t timeout_ms) {
    size_t rx_bits = 0xAA, i;
    FuriHalNfcEvent event;
    FuriHalNfcError error = FuriHalNfcErrorNone;
    uint8_t* data = (uint8_t*)bit_buffer_get_data(cmd);
    if(add_crc == true) {
        switch(g_NfcTech) {
        case FuriHalNfcTechIso14443a:
            iso14443_crc_append(Iso14443CrcTypeA, cmd);
            break;
        case FuriHalNfcTechIso14443b:
            iso14443_crc_append(Iso14443CrcTypeB, cmd);
            break;
        case FuriHalNfcTechIso15693:
            iso13239_crc_append(Iso13239CrcTypeDefault, cmd);
        default:
            break;
        }
    }

#ifdef NFC_CLI_VERBOSE
    for(i = 0; i < bit_buffer_get_size_bytes(cmd); i++) {
        printf("%02X", data[i]);
    }
    printf("\r\n");
#endif // NFC_CLI_VERBOSE

    furi_hal_nfc_acquire();
    furi_hal_nfc_trx_reset();

    furi_hal_nfc_poller_tx(data, bit_buffer_get_size(cmd));

    while(timeout_ms > 0) {
        event = furi_hal_nfc_wait_event_common(10);
#ifdef NFC_CLI_VERBOSE
        printf("\t->wait 0x%X\n", event);
#endif // NFC_CLI_VERBOSE
        timeout_ms -= 10;
        if(event & FuriHalNfcEventRxEnd) {
            break;
        }
    }
    if(timeout_ms != 0) {
        error = furi_hal_nfc_poller_rx(data, 100, &rx_bits);
        if(error != FuriHalNfcErrorNone) {
            printf("Error. >2 - r %d - size %d - data ", error, rx_bits);
            return;
        }

        for(i = 0; i < (rx_bits / 8); i++) {
            printf("%02X", data[i]);
        }
        printf("\r\n");
    } else {
        printf("Error. Timeout\r\n");
    }

    furi_hal_nfc_release();
}

static void add_crc(BitBuffer* cmd) {
    switch(g_NfcTech) {
    case FuriHalNfcTechIso14443a:
        iso14443_crc_append(Iso14443CrcTypeA, cmd);
        break;
    case FuriHalNfcTechIso14443b:
        iso14443_crc_append(Iso14443CrcTypeB, cmd);
        break;
    case FuriHalNfcTechIso15693:
        iso13239_crc_append(Iso13239CrcTypeDefault, cmd);
    default:
        break;
    }
}

static void nfc_cli_send(FuriString* args) {
    UNUSED(args);

    FuriString* para;
    int length = 0, add_crc = 0;
    BitBuffer* cmd = bit_buffer_alloc(256);
    uint8_t* data = (uint8_t*)bit_buffer_get_data(cmd);
    uint32_t timeout_ms = 1000;

    if(!field_on) {
        printf("Error. NFC Field not activated.\r\n");
        return;
    }

    // Check if nfc worker is not busy
    if(furi_hal_nfc_is_hal_ready() != FuriHalNfcErrorNone) {
        printf("Error. NFC chip failed to start\r\n");
        return;
    }

    if(!args_read_int_and_trim(args, &add_crc) || (add_crc > 2)) {
        printf("Error. Incorrect or missing crc value, expected int 0 or 1\r\n");
        return;
    }
#ifdef NFC_CLI_VERBOSE
    printf("CRC: %d\r\n", add_crc);
#endif // NFC_CLI_VERBOSE

    para = furi_string_alloc();
    if(!args_read_string_and_trim(args, para)) {
        printf("Error. No command found!!!\r\n");
        return;
    }

    length = (int)args_length(para);
#ifdef NFC_CLI_VERBOSE
    printf("Command length: %d characters\r\n", length);
#endif // NFC_CLI_VERBOSE
    length /= 2;
    bit_buffer_set_size(cmd, length * 8);

    if(!args_read_hex_bytes(para, data, length)) {
        printf("Error. Command hex byte conversion error\r\n");
        return;
    }
    nfc_send(cmd, (bool)add_crc, timeout_ms);

    bit_buffer_free(cmd);
}

static bool nfc_emu_get_resp(Cli* cli, BitBuffer* rx_data) {
    char c;
    bool compute_crc = false;
    uint8_t tmp[256];
    size_t len = 0;
    c = cli_getc(cli);
    if(c == 0xA) {
        c = cli_getc(cli);
    } else if(c == 9) {
        return true;
    }

    compute_crc = false;
    if(c == 0x31) {
        compute_crc = true;
    }
    c = cli_getc(cli);

    for(int i = 0; i < 256; i++) {
        if(c == 0xA) {
            break;
        } else {
            tmp[len] = c;
            len += 1;
            c = cli_getc(cli);
        }
    }
    if(len == 0) {
        return true;
    }
    unhexify(tmp, (uint8_t*)bit_buffer_get_data(rx_data), len);
    bit_buffer_set_size(rx_data, len * 4);

    if(compute_crc) {
        add_crc(rx_data);
    }

    return false;
}

static void nfc_run_emu(Cli* cli) {
    BitBuffer* rx_cmd = bit_buffer_alloc(256);
    uint8_t* rx_data = (uint8_t*)bit_buffer_get_data(rx_cmd);
    size_t rx_data_size = 256;
    size_t rx_bits;
    uint32_t fdt = 0;

    if(g_NfcMode != FuriHalNfcModeListener) {
        printf("Error. Set emulation mode first.\r\n");
        return;
    }

    if(furi_hal_nfc_is_hal_ready() != FuriHalNfcErrorNone) {
        printf("Error. NFC chip failed to start\r\n");
        return;
    }

    furi_hal_nfc_acquire();

    nfc_low_power_mode_stop();
    furi_hal_nfc_set_mode(g_NfcMode, g_NfcTech);
    if(g_NfcTech == FuriHalNfcTechIso14443a) {
        furi_hal_nfc_iso14443a_listener_set_col_res_data(g_uid, g_uid_len, g_atqa, g_sak);
        fdt = ISO14443_3A_FDT_LISTEN_FC;
    } else if (g_NfcTech == FuriHalNfcTechIso15693){
        fdt = ISO15693_3_FDT_LISTEN_FC;
    }
    furi_hal_nfc_event_start();

    while(true) {
        FuriHalNfcEvent event = furi_hal_nfc_listener_wait_event(100);
        if(event == FuriHalNfcEventTimeout) {
            if(cli_cmd_interrupt_received(cli)) {
                break;
            }
        }
        if(event & FuriHalNfcEventAbortRequest) {
            break;
        }
        if(event & FuriHalNfcEventFieldOn) {
            printf("on\r\n");
        }
        if(event & FuriHalNfcEventFieldOff) {
            furi_hal_nfc_listener_idle();
            printf("off\r\n");
        }
        if(event & FuriHalNfcEventListenerActive) {
            // Nothing
        }
        if(event & FuriHalNfcEventRxEnd) {
            furi_hal_nfc_timer_block_tx_start(fdt);

            rx_bits = 0;
            furi_hal_nfc_listener_rx(rx_data, rx_data_size, &rx_bits);
            if((rx_bits / 8) != 0) {
                for(size_t i = 0; i < (rx_bits / 8); i++) {
                    printf("%02X", rx_data[i]);
                }
                printf("\r\n");

                if(nfc_emu_get_resp(cli, rx_cmd))
                    break;
                }
                while(furi_hal_nfc_timer_block_tx_is_running()) {
                }
                FuriHalNfcError r = furi_hal_nfc_listener_tx(rx_data, bit_buffer_get_size(rx_cmd));
                if(r != FuriHalNfcErrorNone) {
                    printf("error\r\n");
                }
            }
        }
    furi_hal_nfc_reset_mode();
    nfc_low_power_mode_start();
    furi_hal_nfc_release();
}

static void nfc_cli(Cli* cli, FuriString* args, void* context) {
    UNUSED(context);
    FuriString* cli_cmd;
    cli_cmd = furi_string_alloc();
    bool do_free = true;

    do {
        if(!args_read_string_and_trim(args, cli_cmd)) {
            nfc_cli_print_usage();
            break;
        }
        do_free = true;
        if(furi_hal_rtc_is_flag_set(FuriHalRtcFlagDebug)) {
            if(furi_string_cmp_str(cli_cmd, "field") == 0) {
                nfc_cli_field(cli, args);
                break;
            } else if(furi_string_cmp_str(cli_cmd, "on") == 0) {
                nfc_cli_field_on(cli, args);
                break;
            } else if(furi_string_cmp_str(cli_cmd, "mode_14443_a") == 0) {
                nfc_cli_set_tech(FuriHalNfcModePoller, FuriHalNfcTechIso14443a);
                break;
            } else if(furi_string_cmp_str(cli_cmd, "mode_14443_b") == 0) {
                nfc_cli_set_tech(FuriHalNfcModePoller, FuriHalNfcTechIso14443b);
                break;
            } else if(furi_string_cmp_str(cli_cmd, "mode_15693") == 0) {
                nfc_cli_set_tech(FuriHalNfcModePoller, FuriHalNfcTechIso15693);
                break;
            } else if(furi_string_cmp_str(cli_cmd, "reqa") == 0) {
                nfc_cli_reqa(cli, args);
                break;
            } else if(furi_string_cmp_str(cli_cmd, "send") == 0) {
                nfc_cli_send(args);
                break;
            } else if(furi_string_cmp_str(cli_cmd, "off") == 0) {
                nfc_cli_field_off(cli, args);
                break;
            } else if(furi_string_cmp_str(cli_cmd, "mode_emu_14443_a") == 0) {
                nfc_cli_set_tech(FuriHalNfcModeListener, FuriHalNfcTechIso14443a);
                break;
            } else if(furi_string_cmp_str(cli_cmd, "mode_emu_14443_b") == 0) {
                nfc_cli_set_tech(FuriHalNfcModeListener, FuriHalNfcTechIso14443b);
                break;
            } else if(furi_string_cmp_str(cli_cmd, "mode_emu_15693") == 0) {
                nfc_cli_set_tech(FuriHalNfcModeListener, FuriHalNfcTechIso15693);
                break;
            } else if(furi_string_cmp_str(cli_cmd, "set_uid") == 0) {
                nfc_set_uid(args);
                break;
            } else if(furi_string_cmp_str(cli_cmd, "set_sak") == 0) {
                nfc_set_sak(args);
                break;
            } else if(furi_string_cmp_str(cli_cmd, "set_atqa") == 0) {
                nfc_set_atqa(args);
                break;
            } else if(furi_string_cmp_str(cli_cmd, "run_emu") == 0) {
                nfc_run_emu(cli);
                do_free = false;
                break;
            }
        }
        nfc_cli_print_usage();
    } while(false);

    if(do_free) {
        furi_string_free(cli_cmd);
    }
}

void nfc_on_system_start(void) {
#ifdef SRV_CLI
    Cli* cli = furi_record_open(RECORD_CLI);
    cli_add_command(cli, "nfc", CliCommandFlagDefault, nfc_cli, NULL);
    furi_record_close(RECORD_CLI);
#else
    UNUSED(nfc_cli);
#endif
}
