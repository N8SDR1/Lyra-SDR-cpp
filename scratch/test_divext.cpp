// Diversity combiner (divEXT) — synthetic IQ vs the div.c mix.
//
// Loads the bundled wdsp.dll directly. Does not link WdspNative
// (that pulls the app splash / wisdom path). Names here must match
// the hard resolve() strings in src/wdsp_native.cpp.
//
// Rotate is linear, not dB:
//   amp = 10^(gain_dB/20);  θ = phase_deg · π/180
//   path 0 = 1 + j0
//   path 1 = amp · (cos θ + j sin θ)
// Mix (output == nr): out += Irot·I − Qrot·Q , Irot·Q + Qrot·I
// run == 0 copies stream 0. output 0 or 1 copies that stream.
// create_div zeros the rotates; Output(nr) + Run(1) before a
// rotate is silence.

#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QFileInfo>
#include <QtCore/QLibrary>
#include <QtCore/QString>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

int g_fail = 0;

void expect(bool ok, const char* what) {
    if (!ok) {
        ++g_fail;
        std::printf("  FAIL: %s\n", what);
    } else {
        std::printf("  pass: %s\n", what);
    }
}

using fn_GetWDSPVersion_t    = int  (*)();
using fn_create_divEXT_t     = void (*)(int id, int run, int nr, int size);
using fn_destroy_divEXT_t    = void (*)(int id);
using fn_flush_divEXT_t      = void (*)(int id);
using fn_xdivEXT_t           = void (*)(int id, int nsamples,
                                       double** in, double* out);
using fn_SetEXTDIVRun_t      = void (*)(int id, int run);
using fn_SetEXTDIVBuffsize_t = void (*)(int id, int size);
using fn_SetEXTDIVNr_t       = void (*)(int id, int nr);
using fn_SetEXTDIVOutput_t   = void (*)(int id, int output);
using fn_SetEXTDIVRotate_t   = void (*)(int id, int nr,
                                       double* Irotate, double* Qrotate);

struct Api {
    fn_GetWDSPVersion_t    GetWDSPVersion    = nullptr;
    fn_create_divEXT_t     create_divEXT     = nullptr;
    fn_destroy_divEXT_t    destroy_divEXT    = nullptr;
    fn_flush_divEXT_t      flush_divEXT      = nullptr;
    fn_xdivEXT_t           xdivEXT           = nullptr;
    fn_SetEXTDIVRun_t      SetEXTDIVRun      = nullptr;
    fn_SetEXTDIVBuffsize_t SetEXTDIVBuffsize = nullptr;
    fn_SetEXTDIVNr_t       SetEXTDIVNr       = nullptr;
    fn_SetEXTDIVOutput_t   SetEXTDIVOutput   = nullptr;
    fn_SetEXTDIVRotate_t   SetEXTDIVRotate   = nullptr;
};

QString find_wdsp() {
    const QString env = qEnvironmentVariable("LYRA_WDSP_DIR");
    if (!env.isEmpty()) {
        const QString p = QDir(env).filePath("wdsp.dll");
        if (QFileInfo::exists(p))
            return p;
    }
    QDir dir(QCoreApplication::applicationDirPath());
    for (int up = 0; up < 4; ++up) {
        const QString native = dir.filePath("_native/wdsp.dll");
        const QString built  = dir.filePath("build/_native/wdsp.dll");
        if (QFileInfo::exists(native))
            return native;
        if (QFileInfo::exists(built))
            return built;
        if (!dir.cdUp())
            break;
    }
    return {};
}

bool bind(QLibrary& lib, Api& api) {
    auto sym = [&](const char* name) -> void* {
        return lib.resolve(name);
    };
    api.GetWDSPVersion    = reinterpret_cast<fn_GetWDSPVersion_t>(sym("GetWDSPVersion"));
    api.create_divEXT     = reinterpret_cast<fn_create_divEXT_t>(sym("create_divEXT"));
    api.destroy_divEXT    = reinterpret_cast<fn_destroy_divEXT_t>(sym("destroy_divEXT"));
    api.flush_divEXT      = reinterpret_cast<fn_flush_divEXT_t>(sym("flush_divEXT"));
    api.xdivEXT           = reinterpret_cast<fn_xdivEXT_t>(sym("xdivEXT"));
    api.SetEXTDIVRun      = reinterpret_cast<fn_SetEXTDIVRun_t>(sym("SetEXTDIVRun"));
    api.SetEXTDIVBuffsize = reinterpret_cast<fn_SetEXTDIVBuffsize_t>(sym("SetEXTDIVBuffsize"));
    api.SetEXTDIVNr       = reinterpret_cast<fn_SetEXTDIVNr_t>(sym("SetEXTDIVNr"));
    api.SetEXTDIVOutput   = reinterpret_cast<fn_SetEXTDIVOutput_t>(sym("SetEXTDIVOutput"));
    api.SetEXTDIVRotate   = reinterpret_cast<fn_SetEXTDIVRotate_t>(sym("SetEXTDIVRotate"));
    return api.GetWDSPVersion && api.create_divEXT && api.destroy_divEXT
        && api.flush_divEXT && api.xdivEXT && api.SetEXTDIVRun
        && api.SetEXTDIVBuffsize && api.SetEXTDIVNr && api.SetEXTDIVOutput
        && api.SetEXTDIVRotate;
}

constexpr int    kN   = 16;
constexpr int    kNr  = 2;
constexpr double kPi  = 3.14159265358979323846;
constexpr double kTol = 1e-12;

struct Rot { double i; double q; };

Rot from_db(double gainDb, double phaseDeg) {
    const double amp = std::pow(10.0, gainDb / 20.0);
    const double th  = phaseDeg * kPi / 180.0;
    return {amp * std::cos(th), amp * std::sin(th)};
}

void host_mix(const double* a, const double* b, Rot r0, Rot r1, double* out) {
    std::memset(out, 0, sizeof(double) * 2 * kN);
    for (int j = 0; j < kN; ++j) {
        const double I0 = a[2 * j], Q0 = a[2 * j + 1];
        const double I1 = b[2 * j], Q1 = b[2 * j + 1];
        out[2 * j]     += r0.i * I0 - r0.q * Q0;
        out[2 * j + 1] += r0.i * Q0 + r0.q * I0;
        out[2 * j]     += r1.i * I1 - r1.q * Q1;
        out[2 * j + 1] += r1.i * Q1 + r1.q * I1;
    }
}

bool close_to(const double* got, const double* want, int n) {
    for (int i = 0; i < n; ++i) {
        if (std::fabs(got[i] - want[i]) > kTol)
            return false;
    }
    return true;
}

bool near_zero(const double* v, int n) {
    for (int i = 0; i < n; ++i) {
        if (std::fabs(v[i]) > kTol)
            return false;
    }
    return true;
}

void fill_paths(std::vector<double>& a, std::vector<double>& b) {
    a.resize(2 * kN);
    b.resize(2 * kN);
    for (int j = 0; j < kN; ++j) {
        a[2 * j]     = 0.10 * (j + 1);
        a[2 * j + 1] = -0.04 * (j + 1);
        b[2 * j]     = 0.25 * std::sin(0.3 * j);
        b[2 * j + 1] = 0.15 * std::cos(0.3 * j);
    }
}

void run_cases(const Api& api) {
    const int ver = api.GetWDSPVersion();
    std::printf("  GetWDSPVersion = %d\n", ver);
    expect(ver == 210,
           "GetWDSPVersion == 210 (if higher, bump WDSP as its own "
           "commit before mixing it into diversity)");

    std::vector<double> a, b, out(2 * kN), want(2 * kN);
    fill_paths(a, b);
    double* ins[2] = {a.data(), b.data()};

    constexpr int id = 0;
    api.create_divEXT(id, 0, kNr, kN);
    api.flush_divEXT(id);
    api.SetEXTDIVBuffsize(id, kN);
    api.SetEXTDIVNr(id, kNr);

    // run == 0 copies stream 0, even if a rotate is already loaded.
    {
        double ir[2] = {1.0, 1.0};
        double qr[2] = {0.0, 0.0};
        api.SetEXTDIVRotate(id, kNr, ir, qr);
        api.SetEXTDIVOutput(id, kNr);
        api.SetEXTDIVRun(id, 0);
        std::memset(out.data(), 0x5a, out.size() * sizeof(double));
        api.xdivEXT(id, kN, ins, out.data());
        expect(close_to(out.data(), a.data(), 2 * kN),
               "run=0 copies stream 0");
    }

    // Output==nr with still-zero rotates is silence, not a mix.
    {
        api.SetEXTDIVRun(id, 0);
        api.destroy_divEXT(id);
        api.create_divEXT(id, 0, kNr, kN);
        api.SetEXTDIVOutput(id, kNr);
        api.SetEXTDIVRun(id, 1);
        std::memset(out.data(), 0x5a, out.size() * sizeof(double));
        api.xdivEXT(id, kN, ins, out.data());
        expect(near_zero(out.data(), 2 * kN),
               "Output(nr)+Run(1) before rotate is silence");
    }

    auto push = [&](Rot r1) {
        double ir[2] = {1.0, r1.i};
        double qr[2] = {0.0, r1.q};
        api.SetEXTDIVRotate(id, kNr, ir, qr);
        api.SetEXTDIVOutput(id, kNr);
        api.SetEXTDIVRun(id, 1);
        api.xdivEXT(id, kN, ins, out.data());
        host_mix(a.data(), b.data(), Rot{1.0, 0.0}, r1, want.data());
    };

    push(from_db(0.0, 0.0));
    expect(close_to(out.data(), want.data(), 2 * kN),
           "0 dB / 0 deg sums the two paths");

    push(from_db(0.0, 180.0));
    expect(close_to(out.data(), want.data(), 2 * kN),
           "0 dB / 180 deg subtracts path 1");

    // Equal tones at 180 deg null. Linear rotate, not the raw degree.
    {
        std::vector<double> ea(2 * kN), eb(2 * kN), eo(2 * kN);
        for (int j = 0; j < kN; ++j) {
            ea[2 * j] = eb[2 * j] = 0.4;
            ea[2 * j + 1] = eb[2 * j + 1] = -0.2;
        }
        double* eins[2] = {ea.data(), eb.data()};
        const Rot r = from_db(0.0, 180.0);
        double ir[2] = {1.0, r.i};
        double qr[2] = {0.0, r.q};
        api.SetEXTDIVRotate(id, kNr, ir, qr);
        api.xdivEXT(id, kN, eins, eo.data());
        expect(near_zero(eo.data(), 2 * kN),
               "equal tones at 180 deg null");
    }

    const double sixDb = 20.0 * std::log10(2.0);
    push(from_db(sixDb, 0.0));
    expect(close_to(out.data(), want.data(), 2 * kN),
           "+6.0206 dB scales path 1 by 2 (dB is not the rotate)");

    push(from_db(0.0, 90.0));
    expect(close_to(out.data(), want.data(), 2 * kN),
           "90 deg rotates path 1 (I,Q) -> (-Q, I)");

    // run=1, output selects one stream (no mix).
    api.SetEXTDIVOutput(id, 0);
    api.SetEXTDIVRun(id, 1);
    api.xdivEXT(id, kN, ins, out.data());
    expect(close_to(out.data(), a.data(), 2 * kN),
           "output=0 copies stream 0");

    api.SetEXTDIVOutput(id, 1);
    api.xdivEXT(id, kN, ins, out.data());
    expect(close_to(out.data(), b.data(), 2 * kN),
           "output=1 copies stream 1");

    api.SetEXTDIVRun(id, 0);
    api.destroy_divEXT(id);
}

}  // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    std::printf("[test_divext] divEXT rotate vs div.c mix\n");
    const QString path = find_wdsp();
    if (path.isEmpty()) {
        std::printf("  FAIL: wdsp.dll not found "
                    "(set LYRA_WDSP_DIR or run from the repo)\n");
        return 1;
    }
    const QString dllDir = QFileInfo(path).absolutePath();
    qputenv("PATH", (dllDir + QLatin1Char(';')
                     + qEnvironmentVariable("PATH")).toLocal8Bit());
    QLibrary lib(path);
    if (!lib.load()) {
        std::printf("  FAIL: LoadLibrary %s (%s)\n",
                    path.toLocal8Bit().constData(),
                    lib.errorString().toLocal8Bit().constData());
        return 1;
    }
    Api api;
    if (!bind(lib, api)) {
        std::printf("  FAIL: a divEXT export is missing\n");
        return 1;
    }
    std::printf("  pass: nine divEXT exports + GetWDSPVersion\n");
    run_cases(api);
    if (g_fail) {
        std::printf("%d FAIL\n", g_fail);
        return 1;
    }
    std::printf("ALL PASS\n");
    return 0;
}
