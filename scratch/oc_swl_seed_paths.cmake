# OC SWL rows — seed_paths_static.  Run by ctest:
#   cmake -DSRC=<repo>/src -P scratch/oc_swl_seed_paths.cmake
#
# The SWL (broadcast) J16 rows must only ever be stamped by an EXPLICIT
# operator action.  Guards (same body-scan idiom as
# hl2_ioboard_publish_paths.cmake):
# 1. HL2Stream::seedOcSwlN2adr() is called from setOcAccessory (the N2ADR
#    accessory pick, which the Settings "Reload N2ADR HF" button also
#    routes through) and from nowhere else in hl2_stream.cpp — in
#    particular NOT from reloadOcFromDisk (first-run / rig-switch default)
#    and NOT from seedOcN2adr (the ham-only stamp that path uses).
# 2. seedOcN2adr() touches no broadcast row (no bcRow / nBcBands /
#    n2adrBroadcastSeed).
# 3. loadOcSettings() defaults every swl* key to literal 0.
# 4. OcControl::n2adrBroadcastSeed returns 0 for tx (RX-only rows).

if(NOT SRC)
    message(FATAL_ERROR "pass -DSRC=<repo>/src")
endif()

set(_fail 0)

function(_read path out)
    file(READ "${path}" _c)
    string(REPLACE "\r" "" _c "${_c}")
    set(${out} "${_c}" PARENT_SCOPE)
endfunction()

# Body of `<ret> <Class>::<name>(` (definition at line start) up to the
# first closing brace at column 0.
function(_body content cls name out)
    string(REGEX MATCH "\n[A-Za-z0-9:_<> ]*${cls}::${name}\\(" _hit "${content}")
    if(NOT _hit)
        set(${out} "" PARENT_SCOPE)
        return()
    endif()
    string(FIND "${content}" "${_hit}" _pos)
    string(SUBSTRING "${content}" ${_pos} -1 _rest)
    string(FIND "${_rest}" "\n}\n" _end)
    string(SUBSTRING "${_rest}" 0 ${_end} _b)
    set(${out} "${_b}" PARENT_SCOPE)
endfunction()

_read("${SRC}/hl2_stream.cpp" HS)
_read("${SRC}/oc/OcControl.cpp" OC)

# 1. Exactly one call site, and it is setOcAccessory.  (The `;` in a call
#    would split a CMake list, so tag each call first and count the tags.)
string(REGEX REPLACE "[^A-Za-z0-9_:]seedOcSwlN2adr\\(\\);" "@SWLCALL@" _tagged "${HS}")
string(REGEX MATCHALL "@SWLCALL@" _calls "${_tagged}")
list(LENGTH _calls _ncalls)
if(NOT _ncalls EQUAL 1)
    message(SEND_ERROR "swl_seed_paths: expected exactly one seedOcSwlN2adr() call in hl2_stream.cpp, found ${_ncalls}")
    set(_fail 1)
endif()
_body("${HS}" HL2Stream setOcAccessory B)
if(B STREQUAL "")
    message(SEND_ERROR "swl_seed_paths: HL2Stream::setOcAccessory not found")
    set(_fail 1)
elseif(NOT B MATCHES "seedOcSwlN2adr\\(\\);")
    message(SEND_ERROR "swl_seed_paths: setOcAccessory does not call seedOcSwlN2adr()")
    set(_fail 1)
endif()
foreach(fn reloadOcFromDisk seedOcN2adr seedOcEmpty seedOcYaesuExample loadOcSettings)
    _body("${HS}" HL2Stream ${fn} B)
    if(B STREQUAL "")
        message(SEND_ERROR "swl_seed_paths: HL2Stream::${fn} not found")
        set(_fail 1)
    elseif(B MATCHES "seedOcSwlN2adr\\(\\);")
        message(SEND_ERROR "swl_seed_paths: HL2Stream::${fn} must not call seedOcSwlN2adr()")
        set(_fail 1)
    endif()
endforeach()

# 2. The ham-only stamp never reaches a broadcast row.
_body("${HS}" HL2Stream seedOcN2adr B)
if(B MATCHES "bcRow\\(|nBcBands\\(|n2adrBroadcastSeed\\(")
    message(SEND_ERROR "swl_seed_paths: seedOcN2adr touches broadcast rows")
    set(_fail 1)
endif()

# 3. Missing swl* keys load as literal 0.
_body("${HS}" HL2Stream loadOcSettings B)
foreach(k swlRxA swlTxA swlRxB swlTxB)
    if(NOT B MATCHES "\"${k}/%1\"\\)\\.arg\\(bc\\), 0\\)")
        message(SEND_ERROR "swl_seed_paths: loadOcSettings does not default ${k}/N to 0")
        set(_fail 1)
    endif()
endforeach()

# 4. TX seed is 0 — the function opens with `if (tx) return 0;`.
_body("${OC}" OcControl n2adrBroadcastSeed B)
if(B STREQUAL "")
    message(SEND_ERROR "swl_seed_paths: OcControl::n2adrBroadcastSeed not found")
    set(_fail 1)
elseif(NOT B MATCHES "if \\(tx\\)\n *return 0;")
    message(SEND_ERROR "swl_seed_paths: n2adrBroadcastSeed must return 0 for tx")
    set(_fail 1)
endif()

if(_fail)
    message(FATAL_ERROR "swl_seed_paths: FAILED")
endif()
message(STATUS "swl_seed_paths: ok")
