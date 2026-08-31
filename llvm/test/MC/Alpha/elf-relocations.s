# RUN: llvm-mc -triple=alpha-unknown-linux-gnu -filetype=obj %s -o %t.o
# RUN: llvm-readobj --file-headers --sections --symbols --relocations %t.o | FileCheck %s
# RUN: llvm-readobj --hex-dump=.rela.text %t.o | FileCheck %s --check-prefix=NUMBERS

# CHECK: Format: elf64-alpha
# CHECK: Arch: alpha
# CHECK: Machine: EM_ALPHA (0x9026)

# CHECK: Name: .sdata
# CHECK: Flags [ (0x10000003)
# CHECK: Name: .tdata
# CHECK: SHF_TLS

# CHECK: 0x0 R_ALPHA_GPDISP - 0x4
# CHECK: 0x8 R_ALPHA_LITERAL target 0x0
# CHECK: 0xC R_ALPHA_LITUSE - 0x1
# CHECK: 0x10 R_ALPHA_GPRELHIGH .text 0x48
# CHECK: 0x14 R_ALPHA_GPRELLOW .text 0x48
# CHECK: 0x18 R_ALPHA_GPREL16 .text 0x48
# CHECK: 0x1C R_ALPHA_BRSGP target 0x0
# CHECK: 0x20 R_ALPHA_TLSGD tlsvar 0x0
# CHECK: 0x24 R_ALPHA_GOTDTPREL tlsvar 0x0
# CHECK: 0x28 R_ALPHA_DTPRELHI tlsvar 0x0
# CHECK: 0x2C R_ALPHA_DTPRELLO tlsvar 0x0
# CHECK: 0x30 R_ALPHA_DTPREL16 tlsvar 0x0
# CHECK: 0x34 R_ALPHA_GOTTPREL tlsvar 0x0
# CHECK: 0x38 R_ALPHA_TPRELHI tlsvar 0x0
# CHECK: 0x3C R_ALPHA_TPRELLO tlsvar 0x0
# CHECK: 0x40 R_ALPHA_TPREL16 tlsvar 0x0
# CHECK: 0x44 R_ALPHA_LITUSE - 0x4
# CHECK: R_ALPHA_GPREL32 .text 0x48

# CHECK: Name: target
# CHECK: Other [ (0x88)

# Keep raw ABI numbers covered independently of llvm-readobj's relocation-name
# table.  BRSGP is 28, TLSGD is 29, and TPREL16 is the final ABI relocation, 41.
# NUMBERS: 0x00000090 1c000000 00000000 1c000000 {{[0-9a-f]+}}
# NUMBERS: 0x000000b0 1d000000 {{[0-9a-f]+}}
# NUMBERS: 0x00000170 29000000 {{[0-9a-f]+}}

.text
.globl test
test:
  ldah $29,0($27) !gpdisp!1
  lda $29,0($29) !gpdisp!1
  ldq $1,target($29) !literal!2
  addq $1,$2,$3 !lituse_base!2
  ldah $2,target($29) !gprelhigh
  lda $2,target($2) !gprellow
  lda $3,target($29) !gprel
  br $31,target !samegp
  lda $4,tlsvar($29) !tlsgd!3
  lda $5,tlsvar($29) !gotdtprel
  ldah $6,tlsvar($31) !dtprelhi
  lda $6,tlsvar($6) !dtprello
  lda $7,tlsvar($31) !dtprel
  ldq $8,tlsvar($29) !gottprel
  ldah $9,tlsvar($31) !tprelhi
  lda $9,tlsvar($9) !tprello
  lda $10,tlsvar($31) !tprel
  jsr $26,($27),0 !lituse_tlsgd!3
target:
  ret
.usepv target,std

.section .sdata,"aws",@progbits
.globl gpword
gpword:
  .gprel32 target

.section .tdata,"awT",@progbits
.globl tlsvar
.type tlsvar,@tls_object
tlsvar:
  .quad 1
