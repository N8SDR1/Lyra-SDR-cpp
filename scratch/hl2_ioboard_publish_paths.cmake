# HL2 IO board — publish_paths_static (plan HL2_IOBOARD_REMEDIATION_PLAN §5,
# §4.5 path table).  Run by ctest:
#   cmake -DSRC=<repo>/src -P scratch/hl2_ioboard_publish_paths.cmake
#
# 1. Every input-change path in the §4.5 table reaches publishIoboardState():
#    either directly, or through pushEffectiveTxFreq / setHl2RxPort, which
#    must themselves publish directly.
# 2. The snapshot has one writer: outside src/wire/Hl2IoBoard.{h,cpp} the
#    only reference to hl2iob::publish( or g_snapshot is the one call inside
#    HL2Stream::publishIoboardState.

if(NOT SRC)
    message(FATAL_ERROR "pass -DSRC=<repo>/src")
endif()

set(_fail 0)

function(_read path out)
    file(READ "${path}" _c)
    string(REPLACE "\r" "" _c "${_c}")
    set(${out} "${_c}" PARENT_SCOPE)
endfunction()

# Body of `<ret> HL2Stream::<name>(` (definition at line start) up to the
# first closing brace at column 0.
function(_body content name out)
    string(REGEX MATCH "\n[A-Za-z:_<> ]*HL2Stream::${name}\\(" _hit "${content}")
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

# Functions that must call publishIoboardState() directly.
set(_direct
    setRx1FreqHz pushEffectiveTxFreq setSplitEnabled setVfoBHz setSubEnabled
    setDiversityArmed setRx2FreqHz setTxFreqHz setP2DrivePath setHl2RxPort
    setIoboardXvtrAnalog setIoboardSendRxFcodes setIoboardForcePresent open)
foreach(fn IN LISTS _direct)
    _body("${HS}" ${fn} B)
    if(B STREQUAL "")
        message(SEND_ERROR "publish_paths_static: HL2Stream::${fn} not found")
        set(_fail 1)
    elseif(NOT B MATCHES "publishIoboardState\\(\\)")
        message(SEND_ERROR "publish_paths_static: HL2Stream::${fn} does not call publishIoboardState()")
        set(_fail 1)
    endif()
endforeach()

# Functions that reach it through pushEffectiveTxFreq (P3 XIT, P14 slot table).
foreach(fn setXitEnabled setXitOffsetHz setXvtrSlots)
    _body("${HS}" ${fn} B)
    if(B STREQUAL "")
        message(SEND_ERROR "publish_paths_static: HL2Stream::${fn} not found")
        set(_fail 1)
    elseif(NOT B MATCHES "pushEffectiveTxFreq\\(\\)")
        message(SEND_ERROR "publish_paths_static: HL2Stream::${fn} does not reach pushEffectiveTxFreq()")
        set(_fail 1)
    endif()
endforeach()
# P14: the slotsChanged lambda is the path.
_body("${HS}" setXvtrSlots B)
if(NOT B MATCHES "slotsChanged[^\n]*\n[^}]*pushEffectiveTxFreq\\(\\)")
    message(SEND_ERROR "publish_paths_static: setXvtrSlots slotsChanged lambda does not call pushEffectiveTxFreq()")
    set(_fail 1)
endif()

# P11 / P12 / P15: band memory restore and transverter slot apply set the
# RX port through HL2Stream::setHl2RxPort.
foreach(f bandmemory.cpp xvtrslots.cpp)
    _read("${SRC}/${f}" C)
    if(NOT C MATCHES "->setHl2RxPort\\(")
        message(SEND_ERROR "publish_paths_static: ${f} no longer calls setHl2RxPort()")
        set(_fail 1)
    endif()
endforeach()

# Single writer.
file(GLOB_RECURSE _srcs "${SRC}/*.cpp" "${SRC}/*.h")
set(_hits 0)
foreach(f IN LISTS _srcs)
    get_filename_component(_n "${f}" NAME)
    if(_n STREQUAL "Hl2IoBoard.cpp" OR _n STREQUAL "Hl2IoBoard.h")
        continue()
    endif()
    _read("${f}" C)
    string(REGEX MATCHALL "hl2iob::publish\\(|[^A-Za-z0-9_]g_snapshot[^A-Za-z0-9_]" _m "${C}")
    list(LENGTH _m _k)
    if(_k GREATER 0)
        math(EXPR _hits "${_hits} + ${_k}")
        if(NOT _n STREQUAL "hl2_stream.cpp")
            message(SEND_ERROR "publish_paths_static: snapshot writer outside HL2Stream: ${f}")
            set(_fail 1)
        endif()
    endif()
endforeach()
_body("${HS}" publishIoboardState PB)
string(REGEX MATCHALL "hl2iob::publish\\(" _pm "${PB}")
list(LENGTH _pm _pk)
if(NOT _hits EQUAL 1 OR NOT _pk EQUAL 1)
    message(SEND_ERROR "publish_paths_static: expected exactly one hl2iob::publish( (in publishIoboardState), found ${_hits} in src, ${_pk} in publishIoboardState")
    set(_fail 1)
endif()

if(_fail)
    message(FATAL_ERROR "publish_paths_static: FAILED")
endif()
message(STATUS "publish_paths_static: ok")
