/* vita-elf-create appends import/module data to the executable segment.
   Keep one whole page free before writable data, regardless of code size. */
SECTIONS { . = ALIGN(0x1000) + 0x1000; }
INSERT BEFORE .init_array;
