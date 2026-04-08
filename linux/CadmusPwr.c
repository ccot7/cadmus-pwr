/*
 * Created by Cadmus of Tyre (@ccot7) on 4/8/26.
 * CadmusPwr.c — Linux CPU Power Monitor (C/GTK3 GUI)
 *
 * Native Wayland/X11 desktop app. Inspired by Intel Power Gadget.
 * Reads directly from sysfs and procfs. No polling daemons, no DBus.
 *
 * Features:
 *   - Package, core, DRAM, and uncore power via Intel RAPL
 *   - Per-core frequency, utilisation, and estimated power
 *   - Live 60-second rolling graphs with HUD aesthetic
 *   - Thermal throttle detection and warning banner
 *   - Adjustable refresh rate: 250ms / 500ms / 1s / 2s
 *   - Pause / resume
 *   - Click any graph to zoom it full-width
 *   - Per-core row view and heatmap view
 *   - Dark / light theme toggle
 *
 * Build:
 *   sudo dnf install gtk3-devel        # Fedora
 *   sudo apt install libgtk-3-dev      # Debian/Ubuntu
 *   gcc -O2 -o CadmusPwr CadmusPwr.c $(pkg-config --cflags --libs gtk3)
 *
 *   Or simply:  make
 *
 * Run:
 *   ./CadmusPwr          (after udev rule — see README)
 *   sudo ./CadmusPwr     (always works)
 *
 * Permissions:
 *   RAPL counters require elevated read access by default.
 *   See README.md for the permanent udev rule fix.
 */

/* ─── Feature-test macros (must come before any includes) ─────────── */
#define _POSIX_C_SOURCE 200809L

/* ─── Standard headers ───────────────────────────────────────────── */
#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <inttypes.h>

#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <dirent.h>
#include <math.h>

#define PATH_BUF 512

/* ─── Application identity ───────────────────────────────────────── */
#define APP_NAME        "CadmusPwr — Power Monitor"
#define APP_VERSION     "1.0.0"
#define APP_SUBTITLE    "Linux CPU Monitor"

/* ─── Hardware limits ────────────────────────────────────────────── */
#define MAX_CORES           256
#define MAX_THERMAL_ZONES   32
#define MAX_RAPL_DOMAINS    16

/* ─── Behaviour constants ────────────────────────────────────────── */
#define HISTORY_LEN         60      /* seconds of graph history      */
#define THROTTLE_TEMP_C     100.0   /* °C threshold for warning      */
#define HEATMAP_CELL_H      32      /* pixels per heatmap cell row   */
#define GRAPH_HEIGHT_SMALL  100     /* normal 3-up graph height      */
#define GRAPH_HEIGHT_ZOOM   200     /* expanded zoom graph height    */
#define CORE_ROW_HEIGHT     22      /* per-core row height           */

/* ─── Refresh presets ────────────────────────────────────────────── */
static const int REFRESH_PRESETS[] = { 250, 500, 1000, 2000 };
static const int NUM_PRESETS       = 4;
#define DEFAULT_REFRESH_IDX 2       /* 1000ms                        */

/* ═══════════════════════════════════════════════════════════════════
 *  THEME
 * ═══════════════════════════════════════════════════════════════════ */

typedef struct {
    /* Cairo RGB triples (0.0–1.0) */
    double bg0[3];      /* deepest background          */
    double bg1[3];      /* panel / graph background    */
    double bg2[3];      /* card background             */
    double line[3];     /* subtle grid / border        */
    double line_hi[3];  /* accent border               */
    double cyan[3];     /* primary accent              */
    double green[3];    /* low / healthy               */
    double amber[3];    /* moderate                    */
    double red[3];      /* high / critical             */
    double text[3];
    double text_dim[3];
    /* CSS hex strings (for GTK CSS provider) */
    const char *css_bg0;
    const char *css_bg1;
    const char *css_bg2;
    const char *css_border;
    const char *css_cyan;
    const char *css_text;
    const char *css_dim;
    const char *css_status_bg;
    const char *css_status_border;
    const char *css_green;
    const char *css_amber;
    const char *css_red;
} Theme;

static const Theme DARK_THEME = {
    .bg0        = {0.04, 0.05, 0.08},
    .bg1        = {0.07, 0.09, 0.13},
    .bg2        = {0.10, 0.13, 0.19},
    .line       = {0.13, 0.22, 0.35},
    .line_hi    = {0.18, 0.55, 0.90},
    .cyan       = {0.00, 0.85, 1.00},
    .green      = {0.10, 0.90, 0.50},
    .amber      = {1.00, 0.65, 0.00},
    .red        = {1.00, 0.25, 0.28},
    .text       = {0.88, 0.92, 1.00},
    .text_dim   = {0.42, 0.52, 0.68},
    .css_bg0           = "#0a0d14",
    .css_bg1           = "#12171f",
    .css_bg2           = "#1a2130",
    .css_border        = "#1a2e48",
    .css_cyan          = "#00d9ff",
    .css_text          = "#e0ebff",
    .css_dim           = "#6b85ad",
    .css_status_bg     = "#080b10",
    .css_status_border = "#152030",
    .css_green         = "#18e67f",
    .css_amber         = "#ffa600",
    .css_red           = "#ff4047",
};

static const Theme LIGHT_THEME = {
    .bg0        = {0.95, 0.96, 0.98},
    .bg1        = {0.90, 0.92, 0.95},
    .bg2        = {0.98, 0.99, 1.00},
    .line       = {0.75, 0.80, 0.88},
    .line_hi    = {0.20, 0.50, 0.85},
    .cyan       = {0.00, 0.45, 0.80},
    .green      = {0.05, 0.60, 0.25},
    .amber      = {0.80, 0.45, 0.00},
    .red        = {0.85, 0.10, 0.12},
    .text       = {0.10, 0.12, 0.18},
    .text_dim   = {0.38, 0.44, 0.55},
    .css_bg0           = "#f2f4f8",
    .css_bg1           = "#e6eaf0",
    .css_bg2           = "#fafbff",
    .css_border        = "#c0cad8",
    .css_cyan          = "#0073cc",
    .css_text          = "#1a1e2e",
    .css_dim           = "#607090",
    .css_status_bg     = "#dde2ea",
    .css_status_border = "#b0bac8",
    .css_green         = "#0d9945",
    .css_amber         = "#cc7200",
    .css_red           = "#d91820",
};

/* Return the RGB triple for a utilisation/power percentage */
static void theme_data_color(const Theme *t, double pct,
                              double *r, double *g, double *b)
{
    if (pct < 50.0) {
        *r = t->green[0]; *g = t->green[1]; *b = t->green[2];
    } else if (pct < 80.0) {
        *r = t->amber[0]; *g = t->amber[1]; *b = t->amber[2];
    } else {
        *r = t->red[0];   *g = t->red[1];   *b = t->red[2];
    }
}

/* ═══════════════════════════════════════════════════════════════════
 *  DATA STRUCTURES
 * ═══════════════════════════════════════════════════════════════════ */

typedef struct {
    char     name[64];
    char     path[PATH_BUF];
    char     max_path[PATH_BUF];
    uint64_t energy_uj;
    uint64_t max_energy_uj;
    double   watts;
} RaplDomain;

typedef struct {
    uint64_t user, nice, system, idle, iowait, irq, softirq, steal;
} CpuStat;

typedef struct {
    int      id;
    uint64_t freq_khz;
    uint64_t base_freq_khz;
    uint64_t max_freq_khz;
    uint64_t min_freq_khz;
    double   util_pct;
    double   estimated_watts;
    CpuStat  prev_stat;          /* renamed from 'prev' — less ambiguous */
} CoreInfo;

typedef struct {
    char   name[64];
    double temp_c;
} ThermalZone;

typedef struct {
    RaplDomain  domains[MAX_RAPL_DOMAINS];
    int         num_domains;

    CoreInfo    cores[MAX_CORES];
    int         num_cores;

    ThermalZone zones[MAX_THERMAL_ZONES];
    int         num_zones;

    double      pkg_watts;
    double      dram_watts;
    double      core_watts;
    double      uncore_watts;
    double      tdp_watts;
    double      tdp_pct;

    double      pkg_history[HISTORY_LEN];
    double      temp_history[HISTORY_LEN];
    double      util_history[HISTORY_LEN];
    int         history_pos;
    int         history_count;

    double      avg_pkg_watts;
    double      max_pkg_watts;
    uint64_t    sample_count;
    int         throttling;

    char        cpu_model[128];
} SystemState;

/* Which graph is currently zoomed */
typedef enum {
    ZOOM_NONE  = 0,
    ZOOM_POWER = 1,
    ZOOM_TEMP  = 2,
    ZOOM_UTIL  = 3,
} ZoomTarget;

/* ═══════════════════════════════════════════════════════════════════
 *  APP STATE
 * ═══════════════════════════════════════════════════════════════════ */

typedef struct {
    /* Window */
    GtkWidget *window;
    GtkWidget *lbl_cpu_model;

    /* Toolbar */
    GtkWidget *btn_pause;
    GtkWidget *lbl_refresh;
    GtkWidget *slider_refresh;
    GtkWidget *btn_theme;
    GtkWidget *btn_core_view;

    /* Throttle warning */
    GtkWidget *throttle_bar;
    GtkWidget *throttle_label;

    /* Power card */
    GtkWidget *lbl_pkg_watts;
    GtkWidget *lbl_core_watts;
    GtkWidget *lbl_dram_watts;
    GtkWidget *lbl_uncore_watts;
    GtkWidget *lbl_tdp_pct;
    GtkWidget *lbl_avg_watts;

    /* Temperature card */
    GtkWidget *lbl_temps[MAX_THERMAL_ZONES];
    int        num_temp_labels;

    /* Utilization card */
    GtkWidget *lbl_avg_util;

    /* Graphs — normal 3-up row */
    GtkWidget *graph_row;
    GtkWidget *draw_pkg_graph;
    GtkWidget *draw_temp_graph;
    GtkWidget *draw_util_graph;

    /* Zoom overlay */
    GtkWidget *zoom_card;
    GtkWidget *draw_zoom_graph;
    GtkWidget *lbl_zoom_title;

    /* Core views */
    GtkWidget *cores_stack;
    GtkWidget *cores_vbox;
    GtkWidget *core_draw[MAX_CORES];
    GtkWidget *draw_heatmap;
    int        num_core_widgets;

    /* Status bar */
    GtkWidget *lbl_status;

    /* CSS provider — kept so we can reload on theme change */
    GtkCssProvider *css_provider;

    /* Runtime state */
    SystemState *state;
    double       t_prev;
    int          paused;
    int          refresh_ms;
    guint        timer_id;
    ZoomTarget   zoom;
    int          dark_theme;     /* 1 = dark, 0 = light */
    int          heatmap_mode;   /* 1 = heatmap, 0 = rows */
} AppWidgets;

/* Per-core draw callback context — heap-allocated, freed on destroy */
typedef struct {
    AppWidgets *app;
    int         core_idx;
} CoreDrawData;

/* ─── Forward declarations ───────────────────────────────────────── */
static gboolean on_draw_pkg_graph (GtkWidget *, cairo_t *, gpointer);
static gboolean on_draw_temp_graph(GtkWidget *, cairo_t *, gpointer);
static gboolean on_draw_util_graph(GtkWidget *, cairo_t *, gpointer);
static gboolean on_draw_zoom_graph(GtkWidget *, cairo_t *, gpointer);
static gboolean on_draw_core      (GtkWidget *, cairo_t *, gpointer);
static gboolean on_draw_heatmap   (GtkWidget *, cairo_t *, gpointer);
static gboolean on_timer          (gpointer);
static void     apply_theme       (AppWidgets *);
static void     set_zoom          (AppWidgets *, ZoomTarget);

/* ═══════════════════════════════════════════════════════════════════
 *  SYSFS / TIME HELPERS
 * ═══════════════════════════════════════════════════════════════════ */

/* Read a uint64 from a sysfs file. Returns 0 on success, -1 on failure. */
static int read_u64(const char *path, uint64_t *out)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;

    char    buf[32];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);

    if (n <= 0) return -1;
    buf[n] = '\0';
    *out = (uint64_t)strtoull(buf, NULL, 10);
    return 0;
}

/* Read a string from a sysfs file, stripping trailing newline. */
static int read_str(const char *path, char *out, size_t len)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;

    ssize_t n = read(fd, out, len - 1);
    close(fd);

    if (n <= 0) return -1;
    out[n] = '\0';
    char *nl = strchr(out, '\n');
    if (nl) *nl = '\0';
    return 0;
}

/* Monotonic time in seconds. */
static double now_sec(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

/* ═══════════════════════════════════════════════════════════════════
 *  SYSTEM DATA — DISCOVERY
 * ═══════════════════════════════════════════════════════════════════ */

static void read_cpu_model(char *out, size_t len)
{
    FILE *f = fopen("/proc/cpuinfo", "r");
    if (!f) {
        strncpy(out, "Unknown CPU", len - 1);
        out[len - 1] = '\0';
        return;
    }

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "model name", 10) != 0) continue;
        char *colon = strchr(line, ':');
        if (!colon) continue;
        colon += 2;
        strncpy(out, colon, len - 1);
        out[len - 1] = '\0';
        char *nl = strchr(out, '\n');
        if (nl) *nl = '\0';
        fclose(f);
        return;
    }

    fclose(f);
    strncpy(out, "Unknown CPU", len - 1);
    out[len - 1] = '\0';
}

static void discover_rapl(SystemState *s)
{
    const char *base = "/sys/class/powercap";
    DIR *dir = opendir(base);
    if (!dir) return;

    struct dirent *ent;
    s->num_domains = 0;

    while ((ent = readdir(dir)) != NULL &&
           s->num_domains < MAX_RAPL_DOMAINS)
    {
        if (strncmp(ent->d_name, "intel-rapl:", 11) != 0) continue;

        RaplDomain *d = &s->domains[s->num_domains];
        snprintf(d->path,     sizeof(d->path), "%s/%s/energy_uj", base, ent->d_name);
        snprintf(d->max_path, sizeof(d->max_path), "%s/%s/max_energy_range_uj", base, ent->d_name);

        char name_path[PATH_BUF];
        snprintf(name_path, sizeof(name_path), "%s/%s/name", base, ent->d_name);
        if (read_str(name_path, d->name, sizeof(d->name)) < 0)
          snprintf(d->name, sizeof(d->name), "%.63s", ent->d_name);

        read_u64(d->max_path, &d->max_energy_uj);
        if (d->max_energy_uj == 0) d->max_energy_uj = UINT64_MAX;

        read_u64(d->path, &d->energy_uj);
        d->watts = 0.0;
        s->num_domains++;
    }
    closedir(dir);

    /* TDP from package power limit */
    char tdp_path[256];
    snprintf(tdp_path, sizeof(tdp_path),
             "%s/intel-rapl:0/constraint_0_power_limit_uw", base);
    uint64_t tdp_uw = 0;
    if (read_u64(tdp_path, &tdp_uw) == 0 && tdp_uw > 0)
        s->tdp_watts = (double)tdp_uw / 1e6;
}

static void discover_cores(SystemState *s)
{
    s->num_cores = 0;
    char path[PATH_BUF];

    for (int i = 0; i < MAX_CORES; i++) {
        snprintf(path, sizeof(path),
                 "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_cur_freq", i);
        if (access(path, R_OK) != 0) break;

        CoreInfo *c = &s->cores[s->num_cores];
        c->id = i;

        snprintf(path, sizeof(path),
                 "/sys/devices/system/cpu/cpu%d/cpufreq/base_frequency", i);
        read_u64(path, &c->base_freq_khz);

        snprintf(path, sizeof(path),
                 "/sys/devices/system/cpu/cpu%d/cpufreq/cpuinfo_max_freq", i);
        read_u64(path, &c->max_freq_khz);

        snprintf(path, sizeof(path),
                 "/sys/devices/system/cpu/cpu%d/cpufreq/cpuinfo_min_freq", i);
        read_u64(path, &c->min_freq_khz);

        s->num_cores++;
    }
}

static void discover_thermal(SystemState *s)
{
    s->num_zones = 0;
    const char *base = "/sys/class/thermal";
    DIR *dir = opendir(base);
    if (!dir) return;

    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL &&
           s->num_zones < MAX_THERMAL_ZONES)
    {
        if (strncmp(ent->d_name, "thermal_zone", 12) != 0) continue;

        char type_path[PATH_BUF], temp_path[PATH_BUF];
        snprintf(type_path, sizeof(type_path), "%s/%s/type", base, ent->d_name);
        snprintf(temp_path, sizeof(temp_path), "%s/%s/temp", base, ent->d_name);

        ThermalZone *z = &s->zones[s->num_zones];
        if (read_str(type_path, z->name, sizeof(z->name)) < 0) continue;

        if (!strstr(z->name, "x86_pkg") &&
            !strstr(z->name, "acpitz")  &&
            !strstr(z->name, "cpu"))
            continue;

        uint64_t temp_mc = 0;
        if (read_u64(temp_path, &temp_mc) == 0) {
            z->temp_c = (double)temp_mc / 1000.0;
            s->num_zones++;
        }
    }
    closedir(dir);
}

/* ═══════════════════════════════════════════════════════════════════
 *  SYSTEM DATA — UPDATES
 * ═══════════════════════════════════════════════════════════════════ */

static void update_rapl(SystemState *s, double elapsed)
{
    s->pkg_watts = s->dram_watts = s->core_watts = s->uncore_watts = 0.0;

    for (int i = 0; i < s->num_domains; i++) {
        RaplDomain *d = &s->domains[i];
        uint64_t prev = d->energy_uj;
        uint64_t cur  = 0;

        if (read_u64(d->path, &cur) < 0) continue;

        uint64_t delta = (cur >= prev)
            ? cur - prev
            : (d->max_energy_uj - prev) + cur;   /* counter wraparound */

        d->energy_uj = cur;
        d->watts     = ((double)delta / 1e6) / elapsed;

        if (strcmp(d->name, "package-0") == 0 ||
            strcmp(d->name, "package-1") == 0)
            s->pkg_watts    += d->watts;
        else if (strcmp(d->name, "core")   == 0) s->core_watts   += d->watts;
        else if (strcmp(d->name, "dram")   == 0) s->dram_watts   += d->watts;
        else if (strcmp(d->name, "uncore") == 0) s->uncore_watts += d->watts;
    }

    s->tdp_pct = (s->tdp_watts > 0.0)
        ? (s->pkg_watts / s->tdp_watts) * 100.0
        : 0.0;
}

/*
 * Estimate per-core power by distributing the RAPL core domain budget
 * across logical cores weighted by utilisation × (freq / max_freq)².
 * This approximates voltage × current load. Falls back to pkg_watts
 * if the core domain is not exposed by the hardware.
 * These are estimates, not direct measurements.
 */
static void update_per_core_power(SystemState *s)
{
    double budget = (s->core_watts > 0.0) ? s->core_watts : s->pkg_watts;

    if (budget <= 0.0 || s->num_cores == 0) {
        for (int i = 0; i < s->num_cores; i++)
            s->cores[i].estimated_watts = 0.0;
        return;
    }

    double max_freq = 1.0;
    for (int i = 0; i < s->num_cores; i++) {
        if ((double)s->cores[i].max_freq_khz > max_freq)
            max_freq = (double)s->cores[i].max_freq_khz;
    }

    double weights[MAX_CORES];
    double total_weight = 0.0;

    for (int i = 0; i < s->num_cores; i++) {
        double freq_ratio = (s->cores[i].freq_khz > 0 && max_freq > 0)
            ? (double)s->cores[i].freq_khz / max_freq
            : 0.0;
        weights[i]    = (s->cores[i].util_pct / 100.0) * freq_ratio * freq_ratio;
        total_weight += weights[i];
    }

    for (int i = 0; i < s->num_cores; i++) {
        s->cores[i].estimated_watts = (total_weight > 0.0)
            ? (weights[i] / total_weight) * budget
            : budget / (double)s->num_cores;
    }
}

static void read_proc_stat(CpuStat *stats, int max_count, int *count)
{
    FILE *f = fopen("/proc/stat", "r");
    if (!f) return;

    char line[256];
    *count = 0;

    while (fgets(line, sizeof(line), f) != NULL && *count < max_count) {
        if (strncmp(line, "cpu", 3) != 0) continue;
        if (line[3] == ' ') continue;   /* skip aggregate "cpu " line */

        CpuStat *st = &stats[*count];
       sscanf(line, "cpu%*d %" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64,
       &st->user, &st->nice, &st->system, &st->idle,
       &st->iowait, &st->irq, &st->softirq, &st->steal);
        (*count)++;
    }
    fclose(f);
}

static double cpu_util_diff(const CpuStat *prev, const CpuStat *cur)
{
    uint64_t prev_idle  = prev->idle + prev->iowait;
    uint64_t cur_idle   = cur->idle  + cur->iowait;
    uint64_t prev_total = prev->user + prev->nice + prev->system + prev_idle
                        + prev->irq  + prev->softirq + prev->steal;
    uint64_t cur_total  = cur->user  + cur->nice  + cur->system  + cur_idle
                        + cur->irq   + cur->softirq + cur->steal;

    uint64_t d_total = cur_total - prev_total;
    uint64_t d_idle  = cur_idle  - prev_idle;

    if (d_total == 0) return 0.0;
    return 100.0 * (1.0 - (double)d_idle / (double)d_total);
}

static void update_cores(SystemState *s)
{
    CpuStat cur[MAX_CORES];
    int     count = 0;
    read_proc_stat(cur, MAX_CORES, &count);

    char path[PATH_BUF];
    for (int i = 0; i < s->num_cores; i++) {
        CoreInfo *c = &s->cores[i];
        snprintf(path, sizeof(path),
                 "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_cur_freq",
                 c->id);
        read_u64(path, &c->freq_khz);

        if (i < count) {
            c->util_pct  = cpu_util_diff(&c->prev_stat, &cur[i]);
            c->prev_stat = cur[i];
        }
    }

    update_per_core_power(s);
}

static void update_thermal(SystemState *s)
{
    const char *base = "/sys/class/thermal";
    DIR *dir = opendir(base);
    if (!dir) return;

    struct dirent *ent;
    int idx = 0;

    while ((ent = readdir(dir)) != NULL && idx < s->num_zones) {
        if (strncmp(ent->d_name, "thermal_zone", 12) != 0) continue;

        char type_path[PATH_BUF], temp_path[PATH_BUF], type[64] = {0};
        snprintf(type_path, sizeof(type_path), "%s/%s/type", base, ent->d_name);
        snprintf(temp_path, sizeof(temp_path), "%s/%s/temp", base, ent->d_name);

        if (read_str(type_path, type, sizeof(type)) < 0) continue;

        if (!strstr(type, "x86_pkg") &&
            !strstr(type, "acpitz")  &&
            !strstr(type, "cpu"))
            continue;

        uint64_t mc = 0;
        if (read_u64(temp_path, &mc) == 0) {
            s->zones[idx].temp_c = (double)mc / 1000.0;
            idx++;
        }
    }
    closedir(dir);

    /* Throttle detection */
    s->throttling = 0;
    for (int i = 0; i < s->num_zones; i++) {
        if (s->zones[i].temp_c >= THROTTLE_TEMP_C) {
            s->throttling = 1;
            break;
        }
    }
}

static void push_history(SystemState *s)
{
    s->pkg_history[s->history_pos]  = s->pkg_watts;
    s->temp_history[s->history_pos] = (s->num_zones > 0)
        ? s->zones[0].temp_c : 0.0;

    double avg_util = 0.0;
    for (int i = 0; i < s->num_cores; i++)
        avg_util += s->cores[i].util_pct;
    if (s->num_cores > 0) avg_util /= (double)s->num_cores;
    s->util_history[s->history_pos] = avg_util;

    s->history_pos = (s->history_pos + 1) % HISTORY_LEN;
    if (s->history_count < HISTORY_LEN) s->history_count++;

    s->sample_count++;
    s->avg_pkg_watts = (s->avg_pkg_watts * (double)(s->sample_count - 1)
                        + s->pkg_watts) / (double)s->sample_count;
    if (s->pkg_watts > s->max_pkg_watts)
        s->max_pkg_watts = s->pkg_watts;
}

/* ═══════════════════════════════════════════════════════════════════
 *  CAIRO DRAWING PRIMITIVES
 * ═══════════════════════════════════════════════════════════════════ */

/* Four corner brackets — give each graph its HUD feel. */
static void draw_corner_brackets(cairo_t *cr, int w, int h,
                                  int sz, const double color[3])
{
    cairo_set_source_rgba(cr, color[0], color[1], color[2], 0.7);
    cairo_set_line_width(cr, 1.2);

    /* top-left */
    cairo_move_to(cr, 0, sz);     cairo_line_to(cr, 0, 0);
    cairo_line_to(cr, sz, 0);     cairo_stroke(cr);
    /* top-right */
    cairo_move_to(cr, w - sz, 0); cairo_line_to(cr, w, 0);
    cairo_line_to(cr, w, sz);     cairo_stroke(cr);
    /* bottom-left */
    cairo_move_to(cr, 0, h - sz); cairo_line_to(cr, 0, h);
    cairo_line_to(cr, sz, h);     cairo_stroke(cr);
    /* bottom-right */
    cairo_move_to(cr, w - sz, h); cairo_line_to(cr, w, h);
    cairo_line_to(cr, w, h - sz); cairo_stroke(cr);
}

/* Segmented VU-meter style bar (20 segments with gaps). */
static void draw_seg_bar(cairo_t *cr,
                          double x, double y, double w, double h,
                          double pct,
                          const double fill_rgb[3],
                          const double bg_rgb[3])
{
    static const int SEGS = 20;
    double seg_w = (w - (double)(SEGS - 1) * 1.5) / (double)SEGS;
    int    filled = (int)(pct / 100.0 * (double)SEGS + 0.5);

    for (int s = 0; s < SEGS; s++) {
        double sx = x + (double)s * (seg_w + 1.5);
        if (s < filled)
            cairo_set_source_rgba(cr, fill_rgb[0], fill_rgb[1], fill_rgb[2], 0.9);
        else
            cairo_set_source_rgba(cr, bg_rgb[0],   bg_rgb[1],   bg_rgb[2],   0.5);
        cairo_rectangle(cr, sx, y, seg_w, h);
        cairo_fill(cr);
    }
}

/* Full HUD graph: background, scanlines, grid, fill, glow, line, dot, label bar. */
static void draw_hud_graph(cairo_t *cr, int w, int h,
                            const double *history, int count, int pos,
                            double max_val,
                            double r, double g, double b,
                            const char *label, double cur_val,
                            const char *unit,
                            const Theme *t)
{
    /* Panel background */
    cairo_set_source_rgb(cr, t->bg1[0], t->bg1[1], t->bg1[2]);
    cairo_rectangle(cr, 0, 0, w, h);
    cairo_fill(cr);

    /* Scanline texture */
    cairo_set_source_rgba(cr, 0, 0, 0, 0.10);
    cairo_set_line_width(cr, 1.0);
    for (int y = 0; y < h; y += 3) {
        cairo_move_to(cr, 0, y);
        cairo_line_to(cr, w, y);
        cairo_stroke(cr);
    }

    /* Horizontal grid */
    cairo_set_line_width(cr, 0.5);
    for (int i = 1; i < 4; i++) {
        double gy = (double)h * i / 4.0;
        cairo_set_source_rgba(cr, t->line[0], t->line[1], t->line[2], 0.7);
        cairo_move_to(cr, 0, gy); cairo_line_to(cr, w, gy);
        cairo_stroke(cr);
    }
    /* Vertical grid */
    for (int i = 1; i < 6; i++) {
        double gx = (double)w * i / 6.0;
        cairo_set_source_rgba(cr, t->line[0], t->line[1], t->line[2], 0.3);
        cairo_move_to(cr, gx, 0); cairo_line_to(cr, gx, h);
        cairo_stroke(cr);
    }

    if (count < 2 || max_val <= 0.0) goto label_bar;

    {
        int    n         = count;
        int    start_pos = ((pos - n) % HISTORY_LEN + HISTORY_LEN) % HISTORY_LEN;
        double plot_h    = (double)(h - 18);   /* 18px for label bar */

        /* Filled area under the line */
        cairo_set_source_rgba(cr, r, g, b, 0.12);
        cairo_move_to(cr, 0, plot_h);
        for (int i = 0; i < n; i++) {
            int    idx = (start_pos + i) % HISTORY_LEN;
            double x   = (double)i / (double)(HISTORY_LEN - 1) * (double)w;
            double y   = plot_h - (history[idx] / max_val) * (plot_h - 4.0);
            cairo_line_to(cr, x, y);
        }
        cairo_line_to(cr, w, plot_h);
        cairo_close_path(cr);
        cairo_fill(cr);

        /* Glow (thick, dim layer behind the sharp line) */
        cairo_set_source_rgba(cr, r, g, b, 0.20);
        cairo_set_line_width(cr, 4.0);
        for (int i = 0; i < n; i++) {
            int    idx = (start_pos + i) % HISTORY_LEN;
            double x   = (double)i / (double)(HISTORY_LEN - 1) * (double)w;
            double y   = plot_h - (history[idx] / max_val) * (plot_h - 4.0);
            if (i == 0) cairo_move_to(cr, x, y);
            else        cairo_line_to(cr, x, y);
        }
        cairo_stroke(cr);

        /* Sharp bright line on top */
        cairo_set_source_rgba(cr, r, g, b, 0.95);
        cairo_set_line_width(cr, 1.5);
        for (int i = 0; i < n; i++) {
            int    idx = (start_pos + i) % HISTORY_LEN;
            double x   = (double)i / (double)(HISTORY_LEN - 1) * (double)w;
            double y   = plot_h - (history[idx] / max_val) * (plot_h - 4.0);
            if (i == 0) cairo_move_to(cr, x, y);
            else        cairo_line_to(cr, x, y);
        }
        cairo_stroke(cr);

        /* Glowing endpoint dot */
        {
            int    li = (start_pos + n - 1) % HISTORY_LEN;
            double lx = (double)(n - 1) / (double)(HISTORY_LEN - 1) * (double)w;
            double ly = plot_h - (history[li] / max_val) * (plot_h - 4.0);
            cairo_set_source_rgba(cr, r, g, b, 1.0);
            cairo_arc(cr, lx, ly, 3.0, 0, 2 * G_PI); cairo_fill(cr);
            cairo_set_source_rgba(cr, r, g, b, 0.25);
            cairo_arc(cr, lx, ly, 7.0, 0, 2 * G_PI); cairo_fill(cr);
        }
    }

label_bar:;
    /* Bottom label strip */
    cairo_set_source_rgba(cr, t->bg0[0], t->bg0[1], t->bg0[2], 0.85);
    cairo_rectangle(cr, 0, h - 18, w, 18);
    cairo_fill(cr);

    /* Rule above the strip */
    cairo_set_source_rgba(cr, t->line_hi[0], t->line_hi[1], t->line_hi[2], 0.25);
    cairo_set_line_width(cr, 0.5);
    cairo_move_to(cr, 0, h - 18); cairo_line_to(cr, w, h - 18);
    cairo_stroke(cr);

    cairo_select_font_face(cr, "monospace",
                           CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 10);

    /* Left: graph label */
    cairo_set_source_rgba(cr, t->text_dim[0], t->text_dim[1], t->text_dim[2], 0.9);
    cairo_move_to(cr, 5, h - 5);
    cairo_show_text(cr, label);

    /* Right: current value */
    char val_str[32];
    snprintf(val_str, sizeof(val_str), "%.1f %s", cur_val, unit);
    cairo_text_extents_t ext;
    cairo_text_extents(cr, val_str, &ext);
    cairo_set_source_rgba(cr, r, g, b, 1.0);
    cairo_move_to(cr, (double)w - ext.width - 6.0, h - 5);
    cairo_show_text(cr, val_str);

    draw_corner_brackets(cr, w, h, 8, t->cyan);
}

/* ═══════════════════════════════════════════════════════════════════
 *  GRAPH DRAW CALLBACKS
 * ═══════════════════════════════════════════════════════════════════ */

static const double PKG_COLOR[3] = {0.18, 0.60, 1.00};

static void get_temp_color(double temp_c, double *r, double *g, double *b)
{
    if      (temp_c < 60.0) { *r = 0.10; *g = 0.90; *b = 0.50; }
    else if (temp_c < 80.0) { *r = 1.00; *g = 0.65; *b = 0.00; }
    else                    { *r = 1.00; *g = 0.25; *b = 0.28; }
}

static gboolean on_draw_pkg_graph(GtkWidget *widget, cairo_t *cr, gpointer data)
{
    AppWidgets  *app = (AppWidgets *)data;
    SystemState *s   = app->state;
    int w = gtk_widget_get_allocated_width(widget);
    int h = gtk_widget_get_allocated_height(widget);

    double max_w = (s->tdp_watts > 0.0) ? s->tdp_watts * 1.1 : 100.0;
    if (s->max_pkg_watts > max_w) max_w = s->max_pkg_watts * 1.1;

    const Theme *t = app->dark_theme ? &DARK_THEME : &LIGHT_THEME;
    draw_hud_graph(cr, w, h,
                   s->pkg_history, s->history_count, s->history_pos,
                   max_w, PKG_COLOR[0], PKG_COLOR[1], PKG_COLOR[2],
                   "PKG POWER", s->pkg_watts, "W", t);
    return FALSE;
}

static gboolean on_draw_temp_graph(GtkWidget *widget, cairo_t *cr, gpointer data)
{
    AppWidgets  *app  = (AppWidgets *)data;
    SystemState *s    = app->state;
    int w = gtk_widget_get_allocated_width(widget);
    int h = gtk_widget_get_allocated_height(widget);

    double temp = (s->num_zones > 0) ? s->zones[0].temp_c : 0.0;
    double r, g, b;
    get_temp_color(temp, &r, &g, &b);

    const Theme *t = app->dark_theme ? &DARK_THEME : &LIGHT_THEME;
    draw_hud_graph(cr, w, h,
                   s->temp_history, s->history_count, s->history_pos,
                   100.0, r, g, b, "TEMP", temp, "C", t);
    return FALSE;
}

static gboolean on_draw_util_graph(GtkWidget *widget, cairo_t *cr, gpointer data)
{
    AppWidgets  *app = (AppWidgets *)data;
    SystemState *s   = app->state;
    int w = gtk_widget_get_allocated_width(widget);
    int h = gtk_widget_get_allocated_height(widget);

    double avg = 0.0;
    for (int i = 0; i < s->num_cores; i++) avg += s->cores[i].util_pct;
    if (s->num_cores > 0) avg /= (double)s->num_cores;

    double r, g, b;
    const Theme *t = app->dark_theme ? &DARK_THEME : &LIGHT_THEME;
    theme_data_color(t, avg, &r, &g, &b);

    draw_hud_graph(cr, w, h,
                   s->util_history, s->history_count, s->history_pos,
                   100.0, r, g, b, "CPU UTIL", avg, "%", t);
    return FALSE;
}

static gboolean on_draw_zoom_graph(GtkWidget *widget, cairo_t *cr, gpointer data)
{
    AppWidgets  *app = (AppWidgets *)data;
    SystemState *s   = app->state;
    int w = gtk_widget_get_allocated_width(widget);
    int h = gtk_widget_get_allocated_height(widget);

    const Theme *t = app->dark_theme ? &DARK_THEME : &LIGHT_THEME;
    double r, g, b;

    switch (app->zoom) {
    case ZOOM_POWER: {
        double mx = (s->tdp_watts > 0.0) ? s->tdp_watts * 1.1 : 100.0;
        if (s->max_pkg_watts > mx) mx = s->max_pkg_watts * 1.1;
        draw_hud_graph(cr, w, h,
                       s->pkg_history, s->history_count, s->history_pos,
                       mx, PKG_COLOR[0], PKG_COLOR[1], PKG_COLOR[2],
                       "PKG POWER — ZOOMED", s->pkg_watts, "W", t);
        break;
    }
    case ZOOM_TEMP: {
        double temp = (s->num_zones > 0) ? s->zones[0].temp_c : 0.0;
        get_temp_color(temp, &r, &g, &b);
        draw_hud_graph(cr, w, h,
                       s->temp_history, s->history_count, s->history_pos,
                       100.0, r, g, b,
                       "TEMPERATURE — ZOOMED", temp, "C", t);
        break;
    }
    case ZOOM_UTIL: {
        double avg = 0.0;
        for (int i = 0; i < s->num_cores; i++) avg += s->cores[i].util_pct;
        if (s->num_cores > 0) avg /= (double)s->num_cores;
        theme_data_color(t, avg, &r, &g, &b);
        draw_hud_graph(cr, w, h,
                       s->util_history, s->history_count, s->history_pos,
                       100.0, r, g, b,
                       "CPU UTILIZATION — ZOOMED", avg, "%", t);
        break;
    }
    default:
        break;
    }
    return FALSE;
}

/* ─── Graph click handlers ───────────────────────────────────────── */

static gboolean on_graph_click(GtkWidget *widget, GdkEventButton *ev,
                                gpointer data)
{
    if (ev->button != 1) return FALSE;

    AppWidgets *app = (AppWidgets *)data;
    ZoomTarget target = ZOOM_NONE;

    if      (widget == app->draw_pkg_graph)  target = ZOOM_POWER;
    else if (widget == app->draw_temp_graph) target = ZOOM_TEMP;
    else if (widget == app->draw_util_graph) target = ZOOM_UTIL;

    set_zoom(app, (target == app->zoom) ? ZOOM_NONE : target);
    return TRUE;
}

static gboolean on_zoom_click(GtkWidget *widget, GdkEventButton *ev,
                               gpointer data)
{
    (void)widget;
    (void)ev;
    set_zoom((AppWidgets *)data, ZOOM_NONE);
    return TRUE;
}

static void set_zoom(AppWidgets *app, ZoomTarget z)
{
    app->zoom = z;
    if (z == ZOOM_NONE) {
        gtk_widget_hide(app->zoom_card);
        gtk_widget_show(app->graph_row);
    } else {
        gtk_widget_hide(app->graph_row);
        gtk_widget_show(app->zoom_card);
        gtk_widget_queue_draw(app->draw_zoom_graph);
    }
}

/* ═══════════════════════════════════════════════════════════════════
 *  CORE VIEWS
 * ═══════════════════════════════════════════════════════════════════ */

static gboolean on_draw_core(GtkWidget *widget, cairo_t *cr, gpointer data)
{
    const CoreDrawData *cd  = (const CoreDrawData *)data;
    AppWidgets         *app = cd->app;
    SystemState        *s   = app->state;
    int                 idx = cd->core_idx;

    if (idx >= s->num_cores) return FALSE;

    int w = gtk_widget_get_allocated_width(widget);
    int h = gtk_widget_get_allocated_height(widget);

    const CoreInfo *c = &s->cores[idx];
    const Theme    *t = app->dark_theme ? &DARK_THEME : &LIGHT_THEME;

    /* Background */
    cairo_set_source_rgb(cr, t->bg2[0], t->bg2[1], t->bg2[2]);
    cairo_rectangle(cr, 0, 0, w, h);
    cairo_fill(cr);

    /* Left accent bar — colour reflects load */
    double ar, ag, ab;
    theme_data_color(t, c->util_pct, &ar, &ag, &ab);
    cairo_set_source_rgba(cr, ar, ag, ab, 0.9);
    cairo_rectangle(cr, 0, 0, 3, h);
    cairo_fill(cr);

    cairo_select_font_face(cr, "monospace",
                           CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 10);

    /* Core ID */
    char id_str[16];
    snprintf(id_str, sizeof(id_str), "C%02d", idx);
    cairo_set_source_rgba(cr, t->text_dim[0], t->text_dim[1], t->text_dim[2], 1.0);
    cairo_move_to(cr, 8, (double)h / 2.0 + 4.0);
    cairo_show_text(cr, id_str);

    /* Frequency */
    char freq_str[16];
    snprintf(freq_str, sizeof(freq_str), "%.2fG", (double)c->freq_khz / 1e6);
    cairo_set_source_rgba(cr, t->cyan[0], t->cyan[1], t->cyan[2], 0.9);
    cairo_move_to(cr, 36, (double)h / 2.0 + 4.0);
    cairo_show_text(cr, freq_str);

    /* Segmented utilisation bar */
    double seg_r[3] = { ar, ag, ab };
    int bar_x = 86;
    int bar_w = w - bar_x - 90;
    if (bar_w > 20) {
        draw_seg_bar(cr, bar_x, ((double)h - 6.0) / 2.0, bar_w, 6,
                     c->util_pct, seg_r, t->line);
    }

    /* Utilisation % */
    char util_str[8];
    snprintf(util_str, sizeof(util_str), "%3.0f%%", c->util_pct);
    cairo_set_source_rgba(cr, ar, ag, ab, 1.0);
    cairo_text_extents_t ext;
    cairo_text_extents(cr, "100%", &ext);
    cairo_move_to(cr, (double)w - 58.0 - ext.width / 2.0, (double)h / 2.0 + 4.0);
    cairo_show_text(cr, util_str);

    /* Estimated watts */
    char pwr_str[12];
    snprintf(pwr_str, sizeof(pwr_str), "~%.2fW", c->estimated_watts);
    cairo_set_source_rgba(cr, t->amber[0], t->amber[1], t->amber[2], 0.85);
    cairo_text_extents(cr, pwr_str, &ext);
    cairo_move_to(cr, (double)w - ext.width - 6.0, (double)h / 2.0 + 4.0);
    cairo_show_text(cr, pwr_str);

    /* Bottom separator */
    cairo_set_source_rgba(cr, t->line[0], t->line[1], t->line[2], 0.4);
    cairo_set_line_width(cr, 0.5);
    cairo_move_to(cr, 0, (double)h - 0.5);
    cairo_line_to(cr, w, (double)h - 0.5);
    cairo_stroke(cr);

    return FALSE;
}

static gboolean on_draw_heatmap(GtkWidget *widget, cairo_t *cr, gpointer data)
{
    AppWidgets  *app = (AppWidgets *)data;
    SystemState *s   = app->state;
    int w = gtk_widget_get_allocated_width(widget);
    int h = gtk_widget_get_allocated_height(widget);

    const Theme *t = app->dark_theme ? &DARK_THEME : &LIGHT_THEME;

    cairo_set_source_rgb(cr, t->bg2[0], t->bg2[1], t->bg2[2]);
    cairo_rectangle(cr, 0, 0, w, h);
    cairo_fill(cr);

    int n = s->num_cores;
    if (n == 0) return FALSE;

    int cols   = (w > 600) ? 16 : (w > 400) ? 8 : 4;
    if (cols > n) cols = n;
    int rows   = (n + cols - 1) / cols;
    int cell_w = (w - 2) / cols;
    int cell_h = (h - 2) / rows;
    if (cell_h < 8) cell_h = 8;

    cairo_select_font_face(cr, "monospace",
                           CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);

    for (int i = 0; i < n; i++) {
        int col  = i % cols;
        int row  = i / cols;
        int cx   = 1 + col * cell_w;
        int cy   = 1 + row * cell_h;
        int cw   = cell_w - 1;
        int ch   = cell_h - 1;

        double pct = s->cores[i].util_pct;

        /* Interpolate green → amber → red */
        double fr, fg, fb;
        if (pct < 50.0) {
            double f = pct / 50.0;
            fr = t->green[0] + (t->amber[0] - t->green[0]) * f;
            fg = t->green[1] + (t->amber[1] - t->green[1]) * f;
            fb = t->green[2] + (t->amber[2] - t->green[2]) * f;
        } else {
            double f = (pct - 50.0) / 50.0;
            fr = t->amber[0] + (t->red[0] - t->amber[0]) * f;
            fg = t->amber[1] + (t->red[1] - t->amber[1]) * f;
            fb = t->amber[2] + (t->red[2] - t->amber[2]) * f;
        }

        double alpha = (pct < 1.0) ? 0.15 : 0.80;
        cairo_set_source_rgba(cr, fr, fg, fb, alpha);
        cairo_rectangle(cr, cx, cy, cw, ch);
        cairo_fill(cr);

        /* Core label */
        if (cell_w >= 30 && cell_h >= 14) {
            cairo_set_font_size(cr, 9);
            cairo_set_source_rgba(cr, t->bg0[0], t->bg0[1], t->bg0[2], 0.9);
            char lbl[8];
            snprintf(lbl, sizeof(lbl), "C%d", i);
            cairo_move_to(cr, cx + 3, cy + ch - 4);
            cairo_show_text(cr, lbl);
        }

        /* Utilisation % overlay */
        if (cell_w >= 36 && cell_h >= 22) {
            cairo_set_font_size(cr, 8);
            char pstr[6];
            snprintf(pstr, sizeof(pstr), "%.0f%%", pct);
            cairo_set_source_rgba(cr, t->bg0[0], t->bg0[1], t->bg0[2], 0.75);
            cairo_move_to(cr, cx + 3, cy + 10);
            cairo_show_text(cr, pstr);
        }
    }

    /* Grid lines */
    cairo_set_source_rgba(cr, t->bg0[0], t->bg0[1], t->bg0[2], 0.5);
    cairo_set_line_width(cr, 0.5);
    for (int c = 0; c <= cols; c++) {
        double gx = 1.0 + (double)c * cell_w;
        cairo_move_to(cr, gx, 0); cairo_line_to(cr, gx, h);
        cairo_stroke(cr);
    }
    for (int r = 0; r <= rows; r++) {
        double gy = 1.0 + (double)r * cell_h;
        cairo_move_to(cr, 0, gy); cairo_line_to(cr, w, gy);
        cairo_stroke(cr);
    }

    return FALSE;
}

/* ═══════════════════════════════════════════════════════════════════
 *  TOOLBAR CALLBACKS
 * ═══════════════════════════════════════════════════════════════════ */

/* Remove the old timer and, if not paused, register a new one. */
static void reschedule_timer(AppWidgets *app)
{
    if (app->timer_id) {
        g_source_remove(app->timer_id);
        app->timer_id = 0;
    }
    if (!app->paused)
        app->timer_id = g_timeout_add(app->refresh_ms, on_timer, app);
}

static void on_pause_clicked(GtkButton *btn, gpointer data)
{
    (void)btn;
    AppWidgets *app = (AppWidgets *)data;
    app->paused = !app->paused;
    gtk_button_set_label(GTK_BUTTON(app->btn_pause),
                         app->paused ? "RESUME" : "PAUSE");
    reschedule_timer(app);
}

static void on_refresh_changed(GtkRange *range, gpointer data)
{
    AppWidgets *app = (AppWidgets *)data;
    int idx = (int)gtk_range_get_value(range);
    if (idx < 0)           idx = 0;
    if (idx >= NUM_PRESETS) idx = NUM_PRESETS - 1;

    app->refresh_ms = REFRESH_PRESETS[idx];

    char buf[32];
    snprintf(buf, sizeof(buf), "REFRESH: %dms", app->refresh_ms);
    gtk_label_set_text(GTK_LABEL(app->lbl_refresh), buf);

    reschedule_timer(app);
}

static void on_theme_clicked(GtkButton *btn, gpointer data)
{
    (void)btn;
    AppWidgets *app = (AppWidgets *)data;
    app->dark_theme = !app->dark_theme;
    gtk_button_set_label(GTK_BUTTON(app->btn_theme),
                         app->dark_theme ? "LIGHT" : "DARK");
    apply_theme(app);
    gtk_widget_queue_draw(app->window);
}

static void on_core_view_clicked(GtkButton *btn, gpointer data)
{
    (void)btn;
    AppWidgets *app = (AppWidgets *)data;
    app->heatmap_mode = !app->heatmap_mode;
    gtk_button_set_label(GTK_BUTTON(app->btn_core_view),
                         app->heatmap_mode ? "ROW VIEW" : "HEATMAP");
    gtk_stack_set_visible_child_name(GTK_STACK(app->cores_stack),
                                     app->heatmap_mode ? "heatmap" : "rows");
}

/* ═══════════════════════════════════════════════════════════════════
 *  CLEANUP — free heap allocations on window close
 * ═══════════════════════════════════════════════════════════════════ */

/*
 * Each core row gets a heap-allocated CoreDrawData. We store pointers
 * to them in a GPtrArray attached to the window so we can free them
 * when the window is destroyed.
 */
static void on_window_destroy(GtkWidget *widget, gpointer data)
{
    (void)widget;
    AppWidgets *app = (AppWidgets *)data;

    /* Stop the timer */
    if (app->timer_id) {
        g_source_remove(app->timer_id);
        app->timer_id = 0;
    }

    /* Release CSS provider reference */
    if (app->css_provider) {
        g_object_unref(app->css_provider);
        app->css_provider = NULL;
    }

    /* Free all CoreDrawData allocations */
    GPtrArray *cd_array = g_object_get_data(G_OBJECT(app->window), "core-draw-data");
    if (cd_array) g_ptr_array_free(cd_array, TRUE);

    gtk_main_quit();
}

/* ═══════════════════════════════════════════════════════════════════
 *  THEME APPLICATION
 * ═══════════════════════════════════════════════════════════════════ */

static void apply_theme(AppWidgets *app)
{
    const Theme *t = app->dark_theme ? &DARK_THEME : &LIGHT_THEME;

    /* Build CSS from theme palette. Each rule is a separate snprintf
     * so adding new rules never risks overflowing a single buffer. */
    GString *css = g_string_new(NULL);

    g_string_append_printf(css,
        "window { background-color: %s; }", t->css_bg0);
    g_string_append_printf(css,
        "label  { color: %s;"
        "  font-family: 'Noto Mono','DejaVu Sans Mono',monospace; }",
        t->css_text);
    g_string_append_printf(css,
        ".hud-header {"
        "  background-color: %s; border-bottom: 1px solid %s;"
        "  padding: 8px 14px; }",
        t->css_bg1, t->css_border);
    g_string_append_printf(css,
        ".hud-card {"
        "  background-color: %s; border: 1px solid %s;"
        "  border-radius: 4px; padding: 12px; margin: 4px; }",
        t->css_bg1, t->css_border);
    g_string_append_printf(css,
        ".hud-section-title {"
        "  font-family: 'Noto Mono','DejaVu Sans Mono',monospace;"
        "  font-size: 10px; font-weight: bold; color: %s;"
        "  letter-spacing: 3px; padding-bottom: 2px; }",
        t->css_cyan);
    g_string_append_printf(css,
        ".hud-desc {"
        "  font-family: 'Noto Mono','DejaVu Sans Mono',monospace;"
        "  font-size: 10px; color: %s;"
        "  font-style: italic; padding-bottom: 6px; }",
        t->css_dim);
    g_string_append_printf(css,
        ".hud-dim {"
        "  font-family: 'Noto Mono','DejaVu Sans Mono',monospace;"
        "  font-size: 11px; color: %s; }",
        t->css_dim);
    g_string_append_printf(css,
        ".hud-toolbar {"
        "  background-color: %s; border-bottom: 1px solid %s;"
        "  padding: 4px 10px; }",
        t->css_bg1, t->css_border);
    g_string_append_printf(css,
        ".hud-btn {"
        "  font-family: 'Noto Mono','DejaVu Sans Mono',monospace;"
        "  font-size: 10px; font-weight: bold; color: %s;"
        "  background: transparent; border: 1px solid %s;"
        "  border-radius: 2px; padding: 3px 10px; }",
        t->css_cyan, t->css_border);
    g_string_append_printf(css,
        ".hud-btn:hover { background-color: %s; }", t->css_bg2);
    g_string_append_printf(css,
        ".hud-status {"
        "  font-family: 'Noto Mono','DejaVu Sans Mono',monospace;"
        "  font-size: 10px; color: %s; letter-spacing: 1px;"
        "  padding: 5px 12px; background-color: %s;"
        "  border-top: 1px solid %s; }",
        t->css_dim, t->css_status_bg, t->css_status_border);
    g_string_append_printf(css,
        "infobar.error {"
        "  background-color: %s;"
        "  border-top: 1px solid %s; border-bottom: 1px solid %s; }",
        app->dark_theme ? "#2a0a08" : "#fff0f0",
        t->css_red, t->css_red);

    gtk_css_provider_load_from_data(app->css_provider,
                                    css->str, (gssize)css->len, NULL);
    g_string_free(css, TRUE);
}

/* ═══════════════════════════════════════════════════════════════════
 *  TIMER — main 1-second update loop
 * ═══════════════════════════════════════════════════════════════════ */

static gboolean on_timer(gpointer data)
{
    AppWidgets  *app = (AppWidgets *)data;
    SystemState *s   = app->state;

    double t_now   = now_sec();
    double elapsed = t_now - app->t_prev;
    if (elapsed <= 0.0) elapsed = 1.0;
    app->t_prev = t_now;

    update_rapl(s, elapsed);
    update_cores(s);
    update_thermal(s);
    push_history(s);

    const Theme *t = app->dark_theme ? &DARK_THEME : &LIGHT_THEME;
    char buf[256];

    /* ── Throttle warning ── */
    if (s->throttling) {
        double      max_t    = 0.0;
        const char *hot_name = "CPU";
        for (int i = 0; i < s->num_zones; i++) {
            if (s->zones[i].temp_c > max_t) {
                max_t    = s->zones[i].temp_c;
                hot_name = s->zones[i].name;
            }
        }
        snprintf(buf, sizeof(buf),
                 "<b>THERMAL THROTTLING</b>  \xe2\x80\x94  "
                 "%s at %.0f C  \xe2\x80\x94  "
                 "CPU reducing clock speed. Check cooling.",
                 hot_name, max_t);
        gtk_label_set_markup(GTK_LABEL(app->throttle_label), buf);
        gtk_widget_show(app->throttle_bar);
    } else {
        gtk_widget_hide(app->throttle_bar);
    }

    /* ── Package power ── */
    const char *pwr_col = s->pkg_watts < 30.0 ? t->css_green
                        : s->pkg_watts < 60.0 ? t->css_amber
                        :                        t->css_red;
    snprintf(buf, sizeof(buf),
             "<span font=\"28\" weight=\"bold\" foreground=\"%s\""
             " font_family=\"Noto Mono,DejaVu Sans Mono,monospace\">"
             "%.2f</span>"
             "<span font=\"14\" foreground=\"%s\"> W</span>",
             pwr_col, s->pkg_watts, t->css_dim);
    gtk_label_set_markup(GTK_LABEL(app->lbl_pkg_watts), buf);

    if (s->core_watts > 0.0) {
        snprintf(buf, sizeof(buf),
                 "<span foreground=\"%s\">CORES</span>  <b>%.2f W</b>",
                 t->css_cyan, s->core_watts);
        gtk_label_set_markup(GTK_LABEL(app->lbl_core_watts), buf);
        gtk_widget_show(app->lbl_core_watts);
    }
    if (s->dram_watts > 0.0) {
        snprintf(buf, sizeof(buf),
                 "<span foreground=\"#a659ff\">DRAM</span>  <b>%.2f W</b>",
                 s->dram_watts);
        gtk_label_set_markup(GTK_LABEL(app->lbl_dram_watts), buf);
        gtk_widget_show(app->lbl_dram_watts);
    }
    if (s->uncore_watts > 0.0) {
        snprintf(buf, sizeof(buf),
                 "<span foreground=\"#2d99ff\">UNCORE</span>  <b>%.2f W</b>",
                 s->uncore_watts);
        gtk_label_set_markup(GTK_LABEL(app->lbl_uncore_watts), buf);
        gtk_widget_show(app->lbl_uncore_watts);
    }
    if (s->tdp_watts > 0.0) {
        snprintf(buf, sizeof(buf),
                 "<span foreground=\"%s\">TDP</span>  "
                 "<b>%.0f%%</b>  "
                 "<span foreground=\"%s\">of %.0f W</span>",
                 t->css_dim, s->tdp_pct, t->css_dim, s->tdp_watts);
        gtk_label_set_markup(GTK_LABEL(app->lbl_tdp_pct), buf);
    }
    snprintf(buf, sizeof(buf),
             "<span foreground=\"%s\">AVG</span> <b>%.2f W</b>  "
             "<span foreground=\"%s\">PEAK</span> <b>%.2f W</b>",
             t->css_dim, s->avg_pkg_watts,
             t->css_dim, s->max_pkg_watts);
    gtk_label_set_markup(GTK_LABEL(app->lbl_avg_watts), buf);

    /* ── Average utilisation ── */
    {
        double avg = 0.0;
        for (int i = 0; i < s->num_cores; i++) avg += s->cores[i].util_pct;
        if (s->num_cores > 0) avg /= (double)s->num_cores;

        const char *uc = avg < 50.0 ? t->css_green
                       : avg < 80.0 ? t->css_amber
                       :               t->css_red;
        snprintf(buf, sizeof(buf),
                 "<span font=\"28\" weight=\"bold\" foreground=\"%s\""
                 " font_family=\"Noto Mono,DejaVu Sans Mono,monospace\">"
                 "%.1f</span>"
                 "<span font=\"14\" foreground=\"%s\"> %%</span>",
                 uc, avg, t->css_dim);
        gtk_label_set_markup(GTK_LABEL(app->lbl_avg_util), buf);
    }

    /* ── Temperature labels ── */
    for (int i = 0; i < s->num_zones && i < app->num_temp_labels; i++) {
        double      tmp = s->zones[i].temp_c;
        const char *tc  = tmp < 60.0 ? t->css_green
                        : tmp < 80.0 ? t->css_amber
                        :               t->css_red;
        snprintf(buf, sizeof(buf),
                 "<span foreground=\"%s\">%s</span>  "
                 "<span foreground=\"%s\" weight=\"bold\">%.1f C</span>",
                 t->css_dim, s->zones[i].name, tc, tmp);
        gtk_label_set_markup(GTK_LABEL(app->lbl_temps[i]), buf);
    }

    /* ── Graphs ── */
    gtk_widget_queue_draw(app->draw_pkg_graph);
    gtk_widget_queue_draw(app->draw_temp_graph);
    gtk_widget_queue_draw(app->draw_util_graph);
    if (app->zoom != ZOOM_NONE)
        gtk_widget_queue_draw(app->draw_zoom_graph);

    /* ── Core views ── */
    if (app->heatmap_mode) {
        gtk_widget_queue_draw(app->draw_heatmap);
    } else {
        for (int i = 0; i < app->num_core_widgets; i++)
            gtk_widget_queue_draw(app->core_draw[i]);
    }

    /* ── Status bar ── */
    snprintf(buf, sizeof(buf),
             APP_NAME " v" APP_VERSION
             "  \xc2\xb7  SAMPLE #%llu"
             "  \xc2\xb7  %d CORES"
             "  \xc2\xb7  %d RAPL"
             "  \xc2\xb7  %dms%s",
             (unsigned long long)s->sample_count,
             s->num_cores,
             s->num_domains,
             app->refresh_ms,
             app->paused ? "  [PAUSED]" : "");
    gtk_label_set_text(GTK_LABEL(app->lbl_status), buf);

    return G_SOURCE_CONTINUE;
}

/* ═══════════════════════════════════════════════════════════════════
 *  UI HELPERS
 * ═══════════════════════════════════════════════════════════════════ */

static GtkWidget *make_hud_label(const char *css_class, const char *text)
{
    GtkWidget *lbl = gtk_label_new(text);
    gtk_style_context_add_class(gtk_widget_get_style_context(lbl), css_class);
    gtk_label_set_xalign(GTK_LABEL(lbl), 0.0);
    return lbl;
}

static GtkWidget *make_hud_card(void)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_style_context_add_class(gtk_widget_get_style_context(box), "hud-card");
    return box;
}

static GtkWidget *make_hud_btn(const char *label)
{
    GtkWidget *btn = gtk_button_new_with_label(label);
    gtk_style_context_add_class(gtk_widget_get_style_context(btn), "hud-btn");
    gtk_widget_set_focus_on_click(btn, FALSE);
    return btn;
}

/* Pack a child into a box with standard HUD spacing. */
static void hud_pack(GtkWidget *box, GtkWidget *child)
{
    gtk_box_pack_start(GTK_BOX(box), child, FALSE, FALSE, 0);
}

/* ═══════════════════════════════════════════════════════════════════
 *  BUILD UI
 * ═══════════════════════════════════════════════════════════════════ */

static void build_ui(AppWidgets *app)
{
    SystemState *s = app->state;

    /* CSS provider — created once, reloaded on theme change */
    app->css_provider = gtk_css_provider_new();
    gtk_style_context_add_provider_for_screen(
        gdk_screen_get_default(),
        GTK_STYLE_PROVIDER(app->css_provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    apply_theme(app);

    /* Array to track CoreDrawData heap allocations for cleanup */
    GPtrArray *cd_array = g_ptr_array_new_with_free_func(g_free);

    /* ── Window ── */
    app->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(app->window), APP_NAME);
    gtk_window_set_default_size(GTK_WINDOW(app->window), 940, 800);
    gtk_window_set_resizable(GTK_WINDOW(app->window), TRUE);

    /* Store cd_array on window so on_window_destroy can free it */
    g_object_set_data(G_OBJECT(app->window), "core-draw-data", cd_array);
    g_signal_connect(app->window, "destroy",
                     G_CALLBACK(on_window_destroy), app);

    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(app->window), root);

    /* ── Header ── */
    GtkWidget *header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_style_context_add_class(gtk_widget_get_style_context(header),
                                "hud-header");
    gtk_box_pack_start(GTK_BOX(root), header, FALSE, FALSE, 0);

    GtkWidget *title_lbl = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title_lbl),
        "<span font=\"16\" weight=\"bold\" foreground=\"#00d9ff\""
        " font_family=\"Noto Mono,DejaVu Sans Mono,monospace\">"
        APP_NAME "</span>"
        "<span font=\"10\" foreground=\"#2d4a6a\""
        " font_family=\"Noto Mono,DejaVu Sans Mono,monospace\">"
        "  //  " APP_SUBTITLE "</span>");
    gtk_label_set_xalign(GTK_LABEL(title_lbl), 0.0);
    gtk_box_pack_start(GTK_BOX(header), title_lbl, TRUE, TRUE, 0);

    app->lbl_cpu_model = gtk_label_new(s->cpu_model);
    gtk_style_context_add_class(
        gtk_widget_get_style_context(app->lbl_cpu_model), "hud-dim");
    gtk_label_set_xalign(GTK_LABEL(app->lbl_cpu_model), 1.0);
    gtk_box_pack_end(GTK_BOX(header), app->lbl_cpu_model, FALSE, FALSE, 0);

    /* ── Toolbar ── */
    GtkWidget *toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_style_context_add_class(gtk_widget_get_style_context(toolbar),
                                "hud-toolbar");
    gtk_box_pack_start(GTK_BOX(root), toolbar, FALSE, FALSE, 0);

    app->btn_pause = make_hud_btn("PAUSE");
    g_signal_connect(app->btn_pause, "clicked",
                     G_CALLBACK(on_pause_clicked), app);
    gtk_box_pack_start(GTK_BOX(toolbar), app->btn_pause, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(toolbar),
        gtk_separator_new(GTK_ORIENTATION_VERTICAL), FALSE, FALSE, 2);

    app->lbl_refresh = make_hud_label("hud-dim", "REFRESH: 1000ms");
    gtk_box_pack_start(GTK_BOX(toolbar), app->lbl_refresh, FALSE, FALSE, 4);

    app->slider_refresh = gtk_scale_new_with_range(
        GTK_ORIENTATION_HORIZONTAL, 0, NUM_PRESETS - 1, 1);
    gtk_range_set_value(GTK_RANGE(app->slider_refresh), DEFAULT_REFRESH_IDX);
    gtk_scale_set_draw_value(GTK_SCALE(app->slider_refresh), FALSE);
    gtk_widget_set_size_request(app->slider_refresh, 120, -1);
    gtk_widget_set_tooltip_text(app->slider_refresh,
                                "250ms / 500ms / 1000ms / 2000ms");
    g_signal_connect(app->slider_refresh, "value-changed",
                     G_CALLBACK(on_refresh_changed), app);
    gtk_box_pack_start(GTK_BOX(toolbar), app->slider_refresh, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(toolbar),
        gtk_separator_new(GTK_ORIENTATION_VERTICAL), FALSE, FALSE, 2);

    app->btn_core_view = make_hud_btn("HEATMAP");
    g_signal_connect(app->btn_core_view, "clicked",
                     G_CALLBACK(on_core_view_clicked), app);
    gtk_box_pack_start(GTK_BOX(toolbar), app->btn_core_view, FALSE, FALSE, 0);

    app->btn_theme = make_hud_btn("LIGHT");
    g_signal_connect(app->btn_theme, "clicked",
                     G_CALLBACK(on_theme_clicked), app);
    gtk_box_pack_end(GTK_BOX(toolbar), app->btn_theme, FALSE, FALSE, 0);

    GtkWidget *hint = make_hud_label("hud-dim", "click graph to zoom  //");
    gtk_box_pack_end(GTK_BOX(toolbar), hint, FALSE, FALSE, 4);

    /* ── Throttle warning (hidden until needed) ── */
    app->throttle_bar = gtk_info_bar_new();
    gtk_info_bar_set_message_type(GTK_INFO_BAR(app->throttle_bar),
                                  GTK_MESSAGE_ERROR);
    app->throttle_label = gtk_label_new("");
    gtk_label_set_use_markup(GTK_LABEL(app->throttle_label), TRUE);
    gtk_label_set_line_wrap(GTK_LABEL(app->throttle_label), TRUE);
    gtk_container_add(
        GTK_CONTAINER(
            gtk_info_bar_get_content_area(GTK_INFO_BAR(app->throttle_bar))),
        app->throttle_label);
    gtk_box_pack_start(GTK_BOX(root), app->throttle_bar, FALSE, FALSE, 0);
    gtk_widget_hide(app->throttle_bar);

    /* ── Scrollable content ── */
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_box_pack_start(GTK_BOX(root), scroll, TRUE, TRUE, 0);

    GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_margin_start(content,  8);
    gtk_widget_set_margin_end(content,    8);
    gtk_widget_set_margin_top(content,    6);
    gtk_widget_set_margin_bottom(content, 8);
    gtk_container_add(GTK_CONTAINER(scroll), content);

    /* ── 3-column metric cards ── */
    GtkWidget *top_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_pack_start(GTK_BOX(content), top_row, FALSE, FALSE, 0);

    /* POWER card */
    GtkWidget *pwr_card = make_hud_card();
    gtk_widget_set_size_request(pwr_card, 240, -1);
    gtk_box_pack_start(GTK_BOX(top_row), pwr_card, TRUE, TRUE, 0);
    hud_pack(pwr_card, make_hud_label("hud-section-title", "POWER"));
    hud_pack(pwr_card, make_hud_label("hud-desc",
        "Measured via Intel RAPL energy counters."));

    app->lbl_pkg_watts = gtk_label_new("-- W");
    gtk_label_set_use_markup(GTK_LABEL(app->lbl_pkg_watts), TRUE);
    gtk_label_set_xalign(GTK_LABEL(app->lbl_pkg_watts), 0.0);
    hud_pack(pwr_card, app->lbl_pkg_watts);

    app->lbl_tdp_pct = gtk_label_new("");
    gtk_label_set_use_markup(GTK_LABEL(app->lbl_tdp_pct), TRUE);
    gtk_label_set_xalign(GTK_LABEL(app->lbl_tdp_pct), 0.0);
    gtk_box_pack_start(GTK_BOX(pwr_card), app->lbl_tdp_pct, FALSE, FALSE, 4);

    gtk_box_pack_start(GTK_BOX(pwr_card),
        gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 4);

#define MAKE_DOMAIN_LABEL(field) \
    app->field = gtk_label_new(""); \
    gtk_label_set_use_markup(GTK_LABEL(app->field), TRUE); \
    gtk_label_set_xalign(GTK_LABEL(app->field), 0.0); \
    hud_pack(pwr_card, app->field); \
    gtk_widget_hide(app->field);

    MAKE_DOMAIN_LABEL(lbl_core_watts)
    MAKE_DOMAIN_LABEL(lbl_dram_watts)
    MAKE_DOMAIN_LABEL(lbl_uncore_watts)
#undef MAKE_DOMAIN_LABEL

    gtk_box_pack_start(GTK_BOX(pwr_card),
        gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 4);

    app->lbl_avg_watts = gtk_label_new("");
    gtk_label_set_use_markup(GTK_LABEL(app->lbl_avg_watts), TRUE);
    gtk_label_set_xalign(GTK_LABEL(app->lbl_avg_watts), 0.0);
    hud_pack(pwr_card, app->lbl_avg_watts);

    /* TEMPERATURE card */
    GtkWidget *temp_card = make_hud_card();
    gtk_widget_set_size_request(temp_card, 210, -1);
    gtk_box_pack_start(GTK_BOX(top_row), temp_card, TRUE, TRUE, 0);
    hud_pack(temp_card, make_hud_label("hud-section-title", "TEMPERATURE"));
    hud_pack(temp_card, make_hud_label("hud-desc",
        "Die temp. 80 C+ = throttle risk."));

    app->num_temp_labels = 0;
    for (int i = 0; i < s->num_zones && i < 4; i++) {
        app->lbl_temps[i] = gtk_label_new(s->zones[i].name);
        gtk_label_set_use_markup(GTK_LABEL(app->lbl_temps[i]), TRUE);
        gtk_label_set_xalign(GTK_LABEL(app->lbl_temps[i]), 0.0);
        gtk_box_pack_start(GTK_BOX(temp_card),
                           app->lbl_temps[i], FALSE, FALSE, 3);
        app->num_temp_labels++;
    }
    if (s->num_zones == 0)
        hud_pack(temp_card, make_hud_label("hud-dim", "no zones found"));

    /* UTILIZATION card */
    GtkWidget *util_card = make_hud_card();
    gtk_widget_set_size_request(util_card, 210, -1);
    gtk_box_pack_start(GTK_BOX(top_row), util_card, TRUE, TRUE, 0);
    hud_pack(util_card, make_hud_label("hud-section-title", "UTILIZATION"));
    hud_pack(util_card, make_hud_label("hud-desc",
        "% time cores spent doing work vs. idle."));

    app->lbl_avg_util = gtk_label_new(NULL);
    gtk_label_set_use_markup(GTK_LABEL(app->lbl_avg_util), TRUE);
    gtk_label_set_markup(GTK_LABEL(app->lbl_avg_util),
        "<span font=\"28\" weight=\"bold\" foreground=\"#18e67f\""
        " font_family=\"Noto Mono,DejaVu Sans Mono,monospace\">"
        "--</span>"
        "<span font=\"14\" foreground=\"#6b85ad\"> %</span>");
    gtk_label_set_xalign(GTK_LABEL(app->lbl_avg_util), 0.0);
    hud_pack(util_card, app->lbl_avg_util);
    hud_pack(util_card, make_hud_label("hud-dim", "avg across all cores"));

    /* ── Graphs — normal 3-up row ── */
    app->graph_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_pack_start(GTK_BOX(content), app->graph_row, FALSE, FALSE, 0);

#define MAKE_GRAPH_CARD(card_var, draw_var, title_str) \
    { \
        GtkWidget *_card = make_hud_card(); \
        card_var = _card; \
        gtk_box_pack_start(GTK_BOX(app->graph_row), _card, TRUE, TRUE, 0); \
        hud_pack(_card, make_hud_label("hud-section-title", title_str)); \
        draw_var = gtk_drawing_area_new(); \
        gtk_widget_set_size_request(draw_var, -1, GRAPH_HEIGHT_SMALL); \
        gtk_widget_add_events(draw_var, GDK_BUTTON_PRESS_MASK); \
        gtk_box_pack_start(GTK_BOX(_card), draw_var, TRUE, TRUE, 0); \
    }

    GtkWidget *pg_card, *tg_card, *ug_card;   /* local — only needed here */
    MAKE_GRAPH_CARD(pg_card, app->draw_pkg_graph,  "PACKAGE POWER  //  60s")
    MAKE_GRAPH_CARD(tg_card, app->draw_temp_graph, "TEMPERATURE  //  60s")
    MAKE_GRAPH_CARD(ug_card, app->draw_util_graph, "CPU UTILIZATION  //  60s")
    (void)pg_card; (void)tg_card; (void)ug_card;
#undef MAKE_GRAPH_CARD

    g_signal_connect(app->draw_pkg_graph,  "draw",
                     G_CALLBACK(on_draw_pkg_graph),  app);
    g_signal_connect(app->draw_temp_graph, "draw",
                     G_CALLBACK(on_draw_temp_graph), app);
    g_signal_connect(app->draw_util_graph, "draw",
                     G_CALLBACK(on_draw_util_graph), app);
    g_signal_connect(app->draw_pkg_graph,  "button-press-event",
                     G_CALLBACK(on_graph_click), app);
    g_signal_connect(app->draw_temp_graph, "button-press-event",
                     G_CALLBACK(on_graph_click), app);
    g_signal_connect(app->draw_util_graph, "button-press-event",
                     G_CALLBACK(on_graph_click), app);

    /* ── Zoom card (hidden until a graph is clicked) ── */
    app->zoom_card = make_hud_card();
    gtk_box_pack_start(GTK_BOX(content), app->zoom_card, FALSE, FALSE, 0);

    GtkWidget *zh = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    hud_pack(app->zoom_card, zh);
    app->lbl_zoom_title = make_hud_label("hud-section-title", "ZOOM");
    gtk_box_pack_start(GTK_BOX(zh), app->lbl_zoom_title, TRUE, TRUE, 0);
    gtk_box_pack_end(GTK_BOX(zh),
        make_hud_label("hud-dim", "[click to close]"), FALSE, FALSE, 0);

    app->draw_zoom_graph = gtk_drawing_area_new();
    gtk_widget_set_size_request(app->draw_zoom_graph, -1, GRAPH_HEIGHT_ZOOM);
    gtk_widget_add_events(app->draw_zoom_graph, GDK_BUTTON_PRESS_MASK);
    g_signal_connect(app->draw_zoom_graph, "draw",
                     G_CALLBACK(on_draw_zoom_graph), app);
    g_signal_connect(app->draw_zoom_graph, "button-press-event",
                     G_CALLBACK(on_zoom_click), app);
    gtk_box_pack_start(GTK_BOX(app->zoom_card),
                       app->draw_zoom_graph, TRUE, TRUE, 0);
    gtk_widget_hide(app->zoom_card);

    /* ── Cores section ── */
    GtkWidget *cores_card = make_hud_card();
    gtk_box_pack_start(GTK_BOX(content), cores_card, FALSE, FALSE, 0);

    GtkWidget *cores_hdr = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    hud_pack(cores_card, cores_hdr);
    hud_pack(cores_hdr, make_hud_label("hud-section-title", "CORES"));
    hud_pack(cores_hdr,
        make_hud_label("hud-dim", "//  freq / load / util / est.W"));

    hud_pack(cores_card, make_hud_label("hud-desc",
        "Est. W = RAPL budget weighted by util x freq^2. Approximation only."));

    /* GtkStack — crossfade between row and heatmap views */
    app->cores_stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(app->cores_stack),
                                  GTK_STACK_TRANSITION_TYPE_CROSSFADE);
    gtk_stack_set_transition_duration(GTK_STACK(app->cores_stack), 120);
    gtk_box_pack_start(GTK_BOX(cores_card), app->cores_stack, FALSE, FALSE, 0);

    /* Row view */
    app->cores_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    app->num_core_widgets = (s->num_cores < MAX_CORES)
        ? s->num_cores : MAX_CORES;

    for (int i = 0; i < app->num_core_widgets; i++) {
        CoreDrawData *cd = g_new(CoreDrawData, 1);
        cd->app      = app;
        cd->core_idx = i;
        g_ptr_array_add(cd_array, cd);   /* track for cleanup */

        app->core_draw[i] = gtk_drawing_area_new();
        gtk_widget_set_size_request(app->core_draw[i], -1, CORE_ROW_HEIGHT);
        g_signal_connect(app->core_draw[i], "draw",
                         G_CALLBACK(on_draw_core), cd);
        gtk_box_pack_start(GTK_BOX(app->cores_vbox),
                           app->core_draw[i], FALSE, FALSE, 0);
    }
    gtk_stack_add_named(GTK_STACK(app->cores_stack), app->cores_vbox, "rows");

    /* Heatmap view */
    app->draw_heatmap = gtk_drawing_area_new();
    int hm_rows = (app->num_core_widgets + 15) / 16;
    int hm_h    = hm_rows * HEATMAP_CELL_H + 4;
    if (hm_h < 64) hm_h = 64;
    gtk_widget_set_size_request(app->draw_heatmap, -1, hm_h);
    g_signal_connect(app->draw_heatmap, "draw",
                     G_CALLBACK(on_draw_heatmap), app);
    gtk_stack_add_named(GTK_STACK(app->cores_stack),
                        app->draw_heatmap, "heatmap");

    gtk_stack_set_visible_child_name(GTK_STACK(app->cores_stack), "rows");

    /* ── Status bar ── */
    app->lbl_status = gtk_label_new("INITIALIZING...");
    gtk_style_context_add_class(
        gtk_widget_get_style_context(app->lbl_status), "hud-status");
    gtk_label_set_xalign(GTK_LABEL(app->lbl_status), 0.0);
    gtk_box_pack_start(GTK_BOX(root), app->lbl_status, FALSE, FALSE, 0);

    /* Show everything, then hide what starts hidden */
    gtk_widget_show_all(app->window);
    gtk_widget_hide(app->throttle_bar);
    gtk_widget_hide(app->zoom_card);
}

/* ═══════════════════════════════════════════════════════════════════
 *  MAIN
 * ═══════════════════════════════════════════════════════════════════ */

int main(int argc, char *argv[])
{
    gtk_init(&argc, &argv);

    SystemState state;
    memset(&state, 0, sizeof(state));

    read_cpu_model(state.cpu_model, sizeof(state.cpu_model));
    discover_rapl(&state);
    discover_cores(&state);
    discover_thermal(&state);

    if (state.num_domains == 0) {
        GtkWidget *dlg = gtk_message_dialog_new(
            NULL, GTK_DIALOG_MODAL,
            GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE,
            "No RAPL power domains found.\n\n"
            "Try running with sudo, or fix permissions:\n"
            "sudo chmod o+r /sys/class/powercap/intel-rapl:*/energy_uj\n\n"
            "See README.md for the permanent udev rule fix.");
        gtk_dialog_run(GTK_DIALOG(dlg));
        gtk_widget_destroy(dlg);
        return 1;
    }

    /* Prime per-core utilisation counters with a baseline read */
    {
        CpuStat prime[MAX_CORES];
        int     cnt = 0;
        read_proc_stat(prime, MAX_CORES, &cnt);
        for (int i = 0; i < state.num_cores && i < cnt; i++)
            state.cores[i].prev_stat = prime[i];
    }

    AppWidgets app;
    memset(&app, 0, sizeof(app));
    app.state        = &state;
    app.t_prev       = now_sec();
    app.paused       = 0;
    app.refresh_ms   = REFRESH_PRESETS[DEFAULT_REFRESH_IDX];
    app.zoom         = ZOOM_NONE;
    app.dark_theme   = 1;
    app.heatmap_mode = 0;

    build_ui(&app);

    app.timer_id = g_timeout_add(app.refresh_ms, on_timer, &app);

    gtk_main();
    return 0;
}
