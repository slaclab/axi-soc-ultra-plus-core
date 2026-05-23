# axi-soc-ultra-plus-core 

**Documentation:** https://slaclab.github.io/axi-soc-ultra-plus-core/

[DOE Code](https://www.osti.gov/doecode/biblio/75773)

<!--- ######################################################## -->

## Platform debug recipes

### How to force PS_ERROR_OUT for testing only

This procedure will force EM_ERR_ID_CSU_ROM=0x1, which will trigger PS_ERROR_OUT.

EM_ERR_ID_CSU_ROM is BIT0 of pmuErrorToPl[46:0] bus (A.K.A. "JTAG Error Register").

Refer to "JTAG Error Register" on pg 138 of Zynq UltraScale+ Device TRM UG1085 (v2.2).

```bash
xsct
connect
targets -set -nocase -filter {name =~ "*PSU*"}
 mwr -force 0x00FFD80528 0x8000FFFF
disconnect
```

<!--- ######################################################## -->

### How to dump all the PS diagnostic registers

```bash
cd submodule/axi-soc-ultra-plus-core
xsct
source xsct_debug_dump.tcl
```

<!--- ######################################################## -->
