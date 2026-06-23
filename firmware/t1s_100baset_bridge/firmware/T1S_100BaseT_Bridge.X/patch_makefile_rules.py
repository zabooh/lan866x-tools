"""Insert ptp_gm_task.o compile rules into Makefile-default.mk."""
import os

mkFile = r"C:\work\ptp\AN1847\t1s_100baset_bridge\firmware\T1S_100BaseT_Bridge.X\nbproject\Makefile-default.mk"

with open(mkFile, 'r', newline='') as f:
    content = f.read()

# ----- DEBUG compile rule -----
debug_anchor = "\n${OBJECTDIR}/_ext/1360937237/app.o: ../src/app.c  .generated_files/flags/default/b7b8bf0a"

debug_rule = (
    "\n${OBJECTDIR}/_ext/1360937237/ptp_gm_task.o: ../src/ptp_gm_task.c"
    "  .generated_files/flags/default/da39a3ee5e6b4b0d3255bfef95601890afd80709"
    " .generated_files/flags/default/da39a3ee5e6b4b0d3255bfef95601890afd80709\n"
    '\t@${MKDIR} "${OBJECTDIR}/_ext/1360937237" \n'
    "\t@${RM} ${OBJECTDIR}/_ext/1360937237/ptp_gm_task.o.d \n"
    "\t@${RM} ${OBJECTDIR}/_ext/1360937237/ptp_gm_task.o \n"
    "\t${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG   -x c -c"
    " -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -fdata-sections"
    " -O1 -fno-common -DHAVE_CONFIG_H -DWOLFSSL_IGNORE_FILE_WARN"
    ' -I"../src" -I"../src/config/default"'
    ' -I"../src/config/default/library"'
    ' -I"../src/config/default/library/tcpip/src"'
    ' -I"../src/config/default/library/tcpip/src/common"'
    ' -I"../src/packs/ATSAME54P20A_DFP"'
    ' -I"../src/packs/CMSIS/"'
    ' -I"../src/packs/CMSIS/CMSIS/Core/Include"'
    ' -I"../src/third_party/wolfssl"'
    ' -I"../src/third_party/wolfssl/wolfssl"'
    ' -Wall -MP -MMD -MF "${OBJECTDIR}/_ext/1360937237/ptp_gm_task.o.d"'
    " -o ${OBJECTDIR}/_ext/1360937237/ptp_gm_task.o ../src/ptp_gm_task.c"
    "    -DXPRJ_default=$(CND_CONF)    $(COMPARISON_BUILD)   ${PACK_COMMON_OPTIONS} \n"
    "\t\n"
)

# ----- Release compile rule -----
release_anchor = "\n${OBJECTDIR}/_ext/1360937237/app.o: ../src/app.c  .generated_files/flags/default/3951c609"

release_rule = (
    "\n${OBJECTDIR}/_ext/1360937237/ptp_gm_task.o: ../src/ptp_gm_task.c"
    "  .generated_files/flags/default/da39a3ee5e6b4b0d3255bfef95601890afd80709"
    " .generated_files/flags/default/da39a3ee5e6b4b0d3255bfef95601890afd80709\n"
    '\t@${MKDIR} "${OBJECTDIR}/_ext/1360937237" \n'
    "\t@${RM} ${OBJECTDIR}/_ext/1360937237/ptp_gm_task.o.d \n"
    "\t@${RM} ${OBJECTDIR}/_ext/1360937237/ptp_gm_task.o \n"
    "\t${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c"
    " -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -fdata-sections"
    " -O1 -fno-common -DHAVE_CONFIG_H -DWOLFSSL_IGNORE_FILE_WARN"
    ' -I"../src" -I"../src/config/default"'
    ' -I"../src/config/default/library"'
    ' -I"../src/config/default/library/tcpip/src"'
    ' -I"../src/config/default/library/tcpip/src/common"'
    ' -I"../src/packs/ATSAME54P20A_DFP"'
    ' -I"../src/packs/CMSIS/"'
    ' -I"../src/packs/CMSIS/CMSIS/Core/Include"'
    ' -I"../src/third_party/wolfssl"'
    ' -I"../src/third_party/wolfssl/wolfssl"'
    ' -Wall -MP -MMD -MF "${OBJECTDIR}/_ext/1360937237/ptp_gm_task.o.d"'
    " -o ${OBJECTDIR}/_ext/1360937237/ptp_gm_task.o ../src/ptp_gm_task.c"
    "    -DXPRJ_default=$(CND_CONF)    $(COMPARISON_BUILD)   ${PACK_COMMON_OPTIONS} \n"
    "\t\n"
)

assert debug_anchor in content, "DEBUG anchor not found!"
assert release_anchor in content, "Release anchor not found!"
assert "ptp_gm_task.o:" not in content, "ptp_gm_task.o rule already present!"

content = content.replace(debug_anchor, debug_rule + debug_anchor, 1)
content = content.replace(release_anchor, release_rule + release_anchor, 1)

with open(mkFile, 'w', newline='') as f:
    f.write(content)

import re
count = len(re.findall(r'ptp_gm_task', content))
print(f"Done. Total ptp_gm_task occurrences: {count}")
