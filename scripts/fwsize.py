#!/usr/bin/env python3

import math
import os
import subprocess

from ansi.color import fg
from flipper.app import App


PAGE_SIZE = 4096


def radio_gap_bytes(bin_size, image_base, radio_address):
    return radio_address - image_base - bin_size


def radio_layout_fits(bin_size, image_base, radio_address, reserve_pages):
    return (
        radio_gap_bytes(bin_size, image_base, radio_address)
        >= reserve_pages * PAGE_SIZE
    )


class Main(App):
    def init(self):
        self.subparsers = self.parser.add_subparsers(help="sub-command help")

        self.parser_elfsize = self.subparsers.add_parser("elf", help="Dump elf stats")
        self.parser_elfsize.add_argument("elfname", action="store")
        self.parser_elfsize.set_defaults(func=self.process_elf)

        self.parser_binsize = self.subparsers.add_parser("bin", help="Dump bin stats")
        self.parser_binsize.add_argument("binname", action="store")
        self.parser_binsize.add_argument("--radio")
        self.parser_binsize.add_argument(
            "--image-base", type=lambda value: int(value, 0), default=0x08000000
        )
        self.parser_binsize.add_argument("--reserve-pages", type=int, default=0)
        self.parser_binsize.set_defaults(func=self.process_bin)

    def process_elf(self):
        all_sizes = subprocess.check_output(
            ["arm-none-eabi-size", "-A", self.args.elfname], shell=False
        )
        all_sizes = all_sizes.splitlines()

        sections_to_keep = (".text", ".rodata", ".data", ".bss", ".free_flash")
        for line in all_sizes:
            line = line.decode("utf-8")
            parts = line.split()
            if len(parts) != 3:
                continue
            section, size, _ = parts
            if section not in sections_to_keep:
                continue
            print(f"{section:<11} {size:>8} ({(int(size)/1024):6.2f} K)")

        return 0

    def process_bin(self):
        binsize = os.path.getsize(self.args.binname)
        pages = math.ceil(binsize / PAGE_SIZE)
        last_page_state = (binsize % PAGE_SIZE) * 100 / PAGE_SIZE
        print(
            fg.yellow(
                f"{os.path.basename(self.args.binname):<11}: {pages:>4} flash pages (last page {last_page_state:.02f}% full)"
            )
        )
        if self.args.radio:
            from flipper.assets.coprobin import CoproBinary

            radio_address = CoproBinary(self.args.radio).get_flash_load_addr()
            gap = radio_gap_bytes(binsize, self.args.image_base, radio_address)
            print(
                f"radio boundary: 0x{radio_address:08X}, reserve: {gap} bytes "
                f"({gap / PAGE_SIZE:.2f} pages)"
            )
            if not radio_layout_fits(
                binsize,
                self.args.image_base,
                radio_address,
                self.args.reserve_pages,
            ):
                self.logger.error(
                    f"firmware requires {self.args.reserve_pages} reserved page(s) before "
                    f"the radio stack, only {gap} byte(s) remain"
                )
                return 2
        return 0


if __name__ == "__main__":
    Main()()
