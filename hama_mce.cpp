/*
 * Copyright (c) 2010, AaYJFG - XBMC community
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the XBMC community nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL AaYJFG BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <fcntl.h>
#include <signal.h>
#include <getopt.h>
#include <libusb-1.0/libusb.h>

#include "xbmcclient.h"

#define VERSION "1.5"

#define msg(x...) do { fprintf(stdout, x); } while (0)
#define err(x...) do { fprintf(stderr, x); } while (0)

#define PCHK(x...)                                                                      \
    do {										\
        int __err = (x);								\
        if (__err == -1) {								\
            err("*** POSIX ERROR *** in %s:%d [%s]: ", __FILE__, __LINE__, #x);		\
            perror("");									\
            abort();									\
        }										\
    } while (0)

#define UCHK(x...)									\
    do {										\
        int __err = (x);								\
        if (__err < 0)									\
        err("*** USB ERROR *** in %s:%d [%s]: %d\n", __FILE__, __LINE__, #x, __err);	\
    } while (0)


static CXBMCClient xbmc;
static bool disconnected = false;
static bool quit = false;


static void print_ir_code (const char *prefix, struct libusb_transfer *transfer)
{
  msg("** %s : ", prefix);
  for (int i = 0; i < transfer->actual_length; i++)
    msg("%02x ", transfer->buffer[i] & 0xff);
  msg("\n");
}

static void xbmc_action_button (const char *btn)
{
  msg("## send action '%s' (button)\n", btn);
  xbmc.SendACTION(btn, ACTION_BUTTON);
}

static void xbmc_key (const char *key)
{
  msg("## send key (R1) '%s'\n", key);
  xbmc.SendButton(key, "R1", BTN_NO_REPEAT);
}

static void xbmc_keyr (const char *key)
{
  msg("## send repeat key (R1) '%s'\n", key);
  xbmc.SendButton(key, "R1", BTN_DOWN);
}

static void xbmc_release_button ()
{
  msg("## send key released\n");
  xbmc.SendButton(0x01, BTN_UP);
}

static void emit_left_click (void)
{
  msg("## catched button mouse left\n");
  xbmc_key("title");
}

static void emit_right_click (void)
{
  msg("## catched button mouse right\n");
  xbmc_action_button("contextmenu");
}

static void emit_info(void)
{
  msg("## catched button info\n");
  xbmc_key("info");
}

static void emit_mouse (int dx, int dy)
{
  if (dx != 0 || dy != 0) {
      const int maxcoord = 65535;
      static int x = maxcoord / 2;
      static int y = maxcoord / 2;
      x += dx * 64;
      y += dy * 64;
      if (x < 0)
        x = 0;
      if (y < 0)
        y = 0;
      if (x > maxcoord)
        x = maxcoord;
      if (y > maxcoord)
        y = maxcoord;
      msg("## mouse dx %d, dy %d -- x %d y %d\n", dx, dy, x, y);
      xbmc.SendMOUSE(x, y);
  }
}

static void transfer0x81_cb (struct libusb_transfer *transfer)
{
  static const struct {
    char modifier;
    char code;
    bool can_repeat;
    void (*key_fn) (const char *key);
    const char *key;
  } translation_table [] = {
      { 0x03, 0x17, false, xbmc_key, "mytv" }, /* (yellow) TV */
      { 0x01, 0x10, false, xbmc_key, "mymusic" }, /* (blue) Music */
      { 0x01, 0x0c, false, xbmc_key, "mypictures" }, /* (green) Pictures */
      { 0x01, 0x08, false, xbmc_key, "myvideo" }, /* (red) Videos */
      { 0x01, 0x12, false, xbmc_key, "recordedtv" }, /* grey button under yellow */
      { 0x01, 0x0a, false, xbmc_key, "guide" }, /* grey button under blue */
      { 0x01, 0x17, false, xbmc_key, "livetv" }, /* grey button under green */
      { 0x03, 0x10, false, xbmc_key, "menu" }, /* grey button under red */
      { 0x03, 0x05, false, xbmc_key, "reverse" },
      { 0x03, 0x09, false, xbmc_key, "forward" },
      { 0x01, 0x15, false, xbmc_key, "record" },
      { 0x00, 0x2a, false, xbmc_key, "back" },
      { 0x00, 0x28, false, xbmc_key, "select" }, /* ok botton */
      { 0x00, 0x4f, true, xbmc_keyr, "right" },
      { 0x00, 0x50, true, xbmc_keyr, "left" },
      { 0x00, 0x51, true, xbmc_keyr, "down" },
      { 0x00, 0x52, true, xbmc_keyr, "up" },
      { 0x00, 0x4b, true, xbmc_keyr, "pageplus" },
      { 0x00, 0x4e, true, xbmc_keyr, "pageminus" },
      { 0x0c, 0x28, false, xbmc_key, "start" },
      { 0x00, 0x59, false, xbmc_key, "one" },
      { 0x00, 0x5a, false, xbmc_key, "two" },
      { 0x00, 0x5b, false, xbmc_key, "three" },
      { 0x00, 0x5c, false, xbmc_key, "four" },
      { 0x00, 0x5d, false, xbmc_key, "five" },
      { 0x00, 0x5e, false, xbmc_key, "six" },
      { 0x00, 0x5f, false, xbmc_key, "seven" },
      { 0x00, 0x60, false, xbmc_key, "eight" },
      { 0x00, 0x61, false, xbmc_key, "nine" },
      { 0x00, 0x62, false, xbmc_key, "zero" },
      { 0x00, 0x55, false, xbmc_key, "star" },
      { 0x04, 0x5d, false, xbmc_key, "hash" },
      { 0x00, 0x29, false, xbmc_key, "clear" },
      { 0x04, 0x3d, false, xbmc_key, "subtitle" }, /* close button */
      { 0x04, 0x5b, false, NULL, "Alt+3 [discard]" }, /* discard - hash */
      { 0x00, 0x53, false, NULL, "NumLock [discard]" }, /* discard - key down */
      { 0x0c, 0x00, false, NULL, "start [discard]" }, /* discard - mypictures*/
      { 0x08, 0x00, false, NULL, "start [discard]" }, /* discard - start*/
      { 0x04, 0x00, false, NULL, "Alt [discard]" }, /* discard - hash, close*/
      { 0x03, 0x00, false, NULL, "Shift [discard]" }, /* discard - mytv, menu, rewind, fastforward*/
      { 0x01, 0x00, false, NULL, "Ctrl [discard]" }, /* discard - key down */
  };
  static bool repeating = false;
  if (transfer->status == LIBUSB_TRANSFER_NO_DEVICE) {
      disconnected = true;
      return;
  }
  if (transfer->actual_length == 8) {
      char m = transfer->buffer[0];
      char c = transfer->buffer[2];
      if (m == 0x00 && c == 0x00) {
          if (repeating)
            xbmc_release_button();
          goto done;
      }
      for (unsigned int i = 0; i < sizeof(translation_table)/sizeof(translation_table[0]); i++) {
          if (translation_table[i].modifier == m && translation_table[i].code == c) {
              print_ir_code(translation_table[i].key, transfer);
              if (translation_table[i].key_fn != NULL) {
                  translation_table[i].key_fn(translation_table[i].key);
                  repeating = translation_table[i].can_repeat;
              }
              goto done;
          }
      }
  }
  if (transfer->actual_length > 0) {
      msg("*** unknown keycode: ");
      for (int i = 0; i < transfer->actual_length; i++)
        msg("%02x ", transfer->buffer[i] & 0xff);
      msg("\n");
  }
  done:
  UCHK(libusb_submit_transfer(transfer));
}

static void transfer0x82_cb (struct libusb_transfer *transfer)
{
  static const struct {
    char code [5];
    bool can_repeat;
    void (*key_fn) (const char *key);
    const char *key;
  } translation_table [] = {
      { { 0x03, 0x02, 0x55, 0x55, 0x55 }, false, xbmc_key, "power" },
      { { 0x02, 0x02, 0x00, 0x00, 0x55 }, false, xbmc_key, "display" },
      { { 0x02, 0x80, 0x00, 0x00, 0x55 }, false, xbmc_key, "skipminus" },
      { { 0x02, 0x00, 0x02, 0x00, 0x55 }, false, xbmc_key, "skipplus" },
      { { 0x02, 0x00, 0x00, 0x01, 0x55 }, false, xbmc_key, "stop" },
      { { 0x02, 0x00, 0x00, 0x02, 0x55 }, false, xbmc_key, "pause" },
      { { 0x02, 0x00, 0x01, 0x00, 0x55 }, false, xbmc_key, "mute" },
      { { 0x02, 0x10, 0x00, 0x00, 0x55 }, true, xbmc_keyr, "volumeplus" },
      { { 0x02, 0x00, 0x10, 0x00, 0x55 }, true, xbmc_keyr, "volumeminus" },
  };
  static bool repeating = false;
  static int rclick_pending = 0;
  if (transfer->status == LIBUSB_TRANSFER_NO_DEVICE) {
      disconnected = true;
      return;
  }
  if (transfer->status == LIBUSB_TRANSFER_TIMED_OUT) {
      if (rclick_pending == 1) {
          print_ir_code("info", transfer);
          rclick_pending = 0;
          emit_info();
      }
      goto done;
  }
  if (transfer->actual_length == 5) {
      const char keycode_up [5] = { 0x02, 0x00, 0x00, 0x00, 0x55 };
      const char keycode_up_power [5] = { 0x03, 0x00, 0x55, 0x55, 0x55 };
      const char keycode_up_mouse [5] = { 0x01, 0x00, 0x00, 0x00, 0x00 };
      if (memcmp(transfer->buffer, keycode_up, 5) == 0 ||
          memcmp(transfer->buffer, keycode_up_power, 5) == 0 ||
          memcmp(transfer->buffer, keycode_up_mouse, 5) == 0) {
          if (repeating)
            xbmc_release_button();
          if (rclick_pending > 0 && --rclick_pending == 0) {
              print_ir_code("right click", transfer);
              emit_right_click();
          }
          goto done;
      }
      if (transfer->buffer[0] == 0x01) {
          if (transfer->buffer[1] == 0x00) {
              char dx = transfer->buffer[2];
              char dy = transfer->buffer[3];
              if (dx != 0 || dy != 0) {
                  print_ir_code("mouse", transfer);
                  emit_mouse(dx, dy);
              }
          }
          else if (transfer->buffer[1] == 0x01) {
              print_ir_code("left click", transfer);
              emit_left_click();
          }
          else if (transfer->buffer[1] == 0x02) {
              print_ir_code("catching right click/info", transfer);
              rclick_pending = 2;
          }

          goto done;
      }

      for (unsigned int i = 0; i < sizeof(translation_table)/sizeof(translation_table[0]); i++) {
          if (memcmp(translation_table[i].code, transfer->buffer, 5) == 0) {
              print_ir_code(translation_table[i].key, transfer);
              if (translation_table[i].key_fn != NULL) {
                  translation_table[i].key_fn(translation_table[i].key);
                  repeating = translation_table[i].can_repeat;
              }
              goto done;
          }
      }
      msg("*** unknown keycode: ");
      for (int i = 0; i < transfer->actual_length; i++)
        msg("%02x ", transfer->buffer[i] & 0xff);
      msg("\n");
  }
  done:
  UCHK(libusb_submit_transfer(transfer));
}

static void handle_exit(int sig)
{
  quit = true;
}

void print_usage(char* progname)
{
  msg("HAMA MCE remote event client for XBMC\n");
  msg("\tUsage: %s [options]\n", progname);
  msg("\t\t -h --help\t\tdisplay usage summary\n");
  msg("\t\t -v --version\t\tdisplay version\n");
  msg("\t\t -d --daemon\t\trun in background\n");
}

int main (int argc, char **argv)
{
  bool daemonize = false;

  while (true) {
      int c;
      static struct option long_options[] =
          {
              {"help",    no_argument, NULL, 'h'},
              {"version", no_argument, NULL, 'v'},
              {"daemon",  no_argument, NULL, 'd'},
              {"fork",    no_argument, NULL, 'f'},
              {0, 0, 0, 0}
          };

      c = getopt_long(argc, argv, "hvd", long_options, NULL);
      if (c == -1)
        break;
      switch (c) {
      case 'h':
        print_usage(argv[0]);
        exit(EXIT_SUCCESS);
      case 'v':
        msg("HAMA MCE remote event client v%s for XBMC\n", VERSION);
        exit(EXIT_SUCCESS);
      case 'd':
        daemonize = true;
        break;
      default:
        print_usage(argv[0]);
        exit(EXIT_FAILURE);
      }
  }

  if (optind < (argc - 1)) {
      err("%s: too many arguments\n", argv[0]);
      exit(EXIT_FAILURE);
  }

  struct sigaction sa;
  sigemptyset(&sa.sa_mask);
  sa.sa_handler = handle_exit;
  PCHK(sigaction(SIGINT,  &sa, NULL));
  PCHK(sigaction(SIGTERM, &sa, NULL));

  libusb_context *ctx;
  libusb_device_handle *dev;
  struct libusb_transfer *transfer0x81 = libusb_alloc_transfer(0);
  struct libusb_transfer *transfer0x82 = libusb_alloc_transfer(0);
  unsigned char buf0x81 [8];
  unsigned char buf0x82 [5];

  UCHK(libusb_init(&ctx));

  if (!(dev = libusb_open_device_with_vid_pid(ctx, 0x05a4, 0x9881))) {
      err("%s: No HAMA MCE remote control found.\n", argv[0]);
      exit(EXIT_FAILURE);
  }

  int exit_code = EXIT_SUCCESS;

  if (libusb_kernel_driver_active(dev, 0))
    UCHK(libusb_detach_kernel_driver(dev, 0));
  if (libusb_kernel_driver_active(dev, 1))
    UCHK(libusb_detach_kernel_driver(dev, 1));
  UCHK(libusb_claim_interface(dev, 0));
  UCHK(libusb_claim_interface(dev, 1));

  libusb_fill_interrupt_transfer(transfer0x81, dev, 0x81, buf0x81, sizeof(buf0x81), transfer0x81_cb, NULL, 215);
  UCHK(libusb_submit_transfer(transfer0x81));

  libusb_fill_interrupt_transfer(transfer0x82, dev, 0x82, buf0x82, sizeof(buf0x82), transfer0x82_cb, NULL, 200);
  UCHK(libusb_submit_transfer(transfer0x82));

  msg("Connected HAMA MCE Remote\n");
  xbmc.SendHELO("HAMA MCE Remote", ICON_NONE);

  if (daemonize) {
      if (daemon(0,0) == -1) {
          err("Failed to fork\n");
          perror(argv[0]);
          exit_code = EXIT_FAILURE;
          goto exit;
      }
  }

  while (!(disconnected || quit)) {
      UCHK(libusb_handle_events(ctx));
  }
  exit:
  if (disconnected) {
      msg("Disconnected HAMA MCE Remote\n");
      xbmc.SendNOTIFICATION("Disconnected", "HAMA MCE Remote", ICON_NONE);
  }
  else {
      msg("Closing HAMA MCE Remote\n");
      xbmc.SendNOTIFICATION("Closing", "HAMA MCE Remote", ICON_NONE);
  }
  xbmc.SendBYE();

  libusb_free_transfer(transfer0x81);
  libusb_free_transfer(transfer0x82);

  if (!disconnected) {
      // Release the remote back to the system
      UCHK(libusb_release_interface(dev, 0));
      UCHK(libusb_release_interface(dev, 1));
      UCHK(libusb_attach_kernel_driver(dev, 0));
      UCHK(libusb_attach_kernel_driver(dev, 1));
  }

  libusb_close(dev);
  libusb_exit(ctx);

  exit(exit_code);
}
