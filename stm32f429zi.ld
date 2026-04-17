MEMORY
{
  rom (rx) : ORIGIN = 0x08000000, LENGTH = 2048K
  ram (rwx) : ORIGIN = 0x20000000, LENGTH = 192K
  ccmram (rwx) : ORIGIN = 0x10000000, LENGTH = 64K
}
INCLUDE cortex-m-generic.ld