#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <sched.h>
#include <errno.h>
#include <sys/cdefs.h>
#include <sys/types.h>
#include <sys/resource.h>

#include <ctime>
#include <cassert>
#include <iostream>
#include <algorithm>
#include <bitset>
#include <thread>
#include <atomic>

#include <libusb-1.0/libusb.h>

#include "cereal/gen/cpp/car.capnp.h"

#include "common/util.h"
#include "common/params.h"
#include "common/swaglog.h"
#include "common/timing.h"
#include "messaging.hpp"

#include "panda.h"
#include "pandaDFU.hpp"
#include "panda_flashing.hpp"
#include "pigeon.h"

const std::string basedir = util::getenv_default("BASEDIR", "", "/data/pythonpath");

void build_st(std::string target, bool clean, bool output) {
  std::string cmd = "cd " + basedir + "/panda/board";
  if (clean) cmd += " && make clean";
  cmd += " && make " + target;
  if (!output) cmd += " > /dev/null";

  system(cmd.c_str());
}

std::string get_firmware_fn() {
  std::string signed_fn = basedir + "/panda/board/obj/panda.bin.signed";

  if (util::file_exists(signed_fn)) {
    LOGW("Using prebuilt signed firmware");
    return signed_fn;
  } else {
    LOGW("Building panda firmware");
    build_st("obj/panda.bin");
    return basedir + "/panda/board/obj/panda.bin";
  }
}

std::string get_expected_signature() {
  std::string firmware_filename = get_firmware_fn();
  // TODO: check that file exists and has a length > 128
  std::string firmware_contents = util::read_file(firmware_filename);
  std::string firmware_sig = firmware_contents.substr(firmware_contents.length()-128);
  if (firmware_sig.length() != 128) {
    LOGE("Error getting the firmware signature");
  }
  return firmware_sig;
}


void get_out_of_dfu(std::string dfu_serial) {
  try {
    PandaDFU dfuPanda(dfu_serial);
    LOGD("Found DFU panda, running recovery");
    dfuPanda.recover();
  } catch(std::runtime_error &e){
    LOGD("DFU panda not found");
    return;
  }
}

void update_panda(std::string serial, std::string dfu_serial) {
  LOGD("updating panda");
  LOGD("\n1: Move out of DFU\n");
  get_out_of_dfu(dfu_serial);
  LOGD("\n2: Start DynamicPanda and run the required steps\n");

  std::string fw_fn = get_firmware_fn();
  std::string fw_signature = get_expected_signature();
  DynamicPanda tempPanda(serial, dfu_serial);
  std::string panda_signature = tempPanda.bootstub ? "": tempPanda.get_signature();
  LOGD("fw_sig::panda_sig");
  LOGD(fw_signature.c_str());
  LOGD(panda_signature.c_str());

  if (tempPanda.bootstub || panda_signature != fw_signature) {
    LOGW("Panda firmware out of date, update required");
    tempPanda.flash(fw_fn);
    LOGD("Done flashing new firmware");
  }

  if (tempPanda.bootstub) {
    std::string bootstub_version = tempPanda.get_version();
    LOGW("Flashed firmware not booting, flashing development bootloader. Bootstub verstion: ");
    LOGW(bootstub_version.c_str());
    tempPanda.recover();
    LOGD("Done flashing dev bootloader and firmware");
  }

  if (tempPanda.bootstub) {
    LOGW("Panda still not booting, exiting");
    throw std::runtime_error("PANDA NOT BOOTING");
  }

  panda_signature = tempPanda.get_signature();
  if (panda_signature != fw_signature) {
    LOGW("Version mismatch after flashing, exiting");
    throw std::runtime_error("FIRMWARE VERSION MISMATCH");
  }

}