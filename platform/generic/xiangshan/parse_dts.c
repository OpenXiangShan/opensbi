#include <sbi/sbi_string.h>
#include <sbi/sbi_console.h>
#include <sbi/sbi_error.h>
#include <libfdt.h>
#include "parse_dts.h"


/* State Definition */
enum parse_state {
    PARSE_STATE_NONE,
    PARSE_STATE_MEM,
    PARSE_STATE_UART,
    PARSE_STATE_CMD,
    PARSE_STATE_TASK
};

/* Manually implement isspace (only handles spaces and tabs) */
static inline bool is_space(char c)
{
    return (c == ' ' || c == '\t' || c == '\r');
}

/*  Trim leading and trailing whitespace from a string. */
static void trim_line(char *line)
{
    char *start = line;
    char *end;

    /* Skip leading whitespace.  */
    while (*start && is_space(*start))
        start++;

    if (*start == '\0') {
        line[0] = '\0';
        return;
    }

    /*  Trim trailing whitespace. */
    end = start + sbi_strlen(start) - 1;
    while (end > start && is_space(*end))
        end--;

    /*  Copy the string. */
    sbi_memmove(line, start, end - start + 1);
    line[end - start + 1] = '\0';
}

/* Parse a line and update the platform config. */
static unsigned long parse_number(const char *s)
{
    unsigned long val = 0;
    int base = 10;
    const char *p = s;

    /* Skip leading whitespace. */
    while (*p && is_space(*p)) p++;

    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        base = 16;
        p += 2;
    }

    while (*p) {
        char c = *p;
        int digit;

        if (c >= '0' && c <= '9')
            digit = c - '0';
        else if (base == 16 && c >= 'a' && c <= 'f')
            digit = c - 'a' + 10;
        else if (base == 16 && c >= 'A' && c <= 'F')
            digit = c - 'A' + 10;
        else
            break;

        if (digit >= base)
            break;

        val = val * base + digit;
        p++;
    }

    return val;
}

/*  Parse a line and update the platform config. */
static int split_key_value(char *line, char **key, char **val)
{
    char *colon = line;
    while (*colon && *colon != ':')
        colon++;

    if (!*colon)
        return -1;

    *colon = '\0';
    *key = line;
    *val = colon + 1;

    /*  Trim trailing whitespace. */
    char *k = *key;
    while (*k && is_space(*k)) k++;
    *key = k;

    char *v = *val;
    while (*v && is_space(*v)) v++;
    *val = v;

    return 0;
}

/* Parse a line and update the platform config. */
static void extract_quoted_string(const char *val, char *out, u32 out_size)
{
    if (!val || !out || out_size == 0)
        return;

    /* Skip leading whitespace */
    while (*val && is_space(*val))
        val++;

    /* Must start with */
    if (*val != '"') {
        out[0] = '\0';
        return;
    }

    val++; /*  Skip " */
    char *dst = out;
    u32 copied = 0;

    while (*val && *val != '"' && copied < out_size - 1) {
        *dst++ = *val++;
        copied++;
    }

    *dst = '\0';

}

static void parse_line_by_state(enum parse_state state,
                                char *line,
                                struct platform_config *cfg)
{
    char *key, *val;
    unsigned long num;

    if (split_key_value(line, &key, &val) != 0)
        return;

    num = parse_number(val);

    if (state == PARSE_STATE_MEM) {
        if (sbi_strcmp(key, "start_addr") == 0) {
            cfg->mem.start_addr = num;
        } else if (sbi_strcmp(key, "size") == 0) {
            cfg->mem.size = num;
        }
    } else if (state == PARSE_STATE_UART) {
        /* Only process 'uart: compatible_string */
        if (sbi_strcmp(key, "uart") == 0) {
            /* translates to "Remove leading and trailing whitespace from val (trim already done, but just to be safe) */
            char *v = val;
            while (*v && is_space(*v)) v++;
            u32 len = sbi_strlen(v);
            while (len > 0 && is_space(v[len - 1])) len--;
            if (len >= MAX_UART_COMPAT_LEN)
                len = MAX_UART_COMPAT_LEN - 1;
            sbi_memcpy(cfg->uart.compatible, v, len);
            cfg->uart.compatible[len] = '\0';
        }
    } else if (state == PARSE_STATE_CMD) {
        /* Currently only supports bootargs= */
        if (sbi_strcmp(key, "bootargs") == 0) {
            extract_quoted_string(val, cfg->cmd.bootargs, sizeof(cfg->cmd.bootargs));
        }
        if (sbi_strcmp(key, "start_addr") == 0) {
            cfg->cmd.start_addr = num;
        }
    }
}

int parse_platform_config_from_mem(struct platform_config *cfg)
{
    char *config_base = (char *)CONFIG_TEXT_ADDR;
    enum parse_state state = PARSE_STATE_NONE;
    char line[MAX_LINE_LEN];
    u32 offset = 0;
    u32 line_pos = 0;
    u32 line_start_offset = 0;

    sbi_memset(cfg, 0, sizeof(*cfg));

    /* Read character by character and process line by line. */
    while (offset < MAX_CONFIG_SIZE) {
        char c = config_base[offset++];
        if (c == '\0')
            break;

        if (c == '\n' || line_pos >= MAX_LINE_LEN - 1) {
            /* Process a complete line. */
            line[line_pos] = '\0';
            trim_line(line);

            if (line[0] != '\0' && line[0] != '#') {
                /* Check section markers. */
                if (sbi_strcmp(line, "[mem]") == 0) {
                    state = PARSE_STATE_MEM;
                } else if (sbi_strcmp(line, "[mem_end]") == 0) {
                    state = PARSE_STATE_NONE;
                    cfg->mem_valid = true;
                } else if (sbi_strcmp(line, "[uart]") == 0) {
                    state = PARSE_STATE_UART;
                } else if (sbi_strcmp(line, "[uart_end]") == 0) {
                    state = PARSE_STATE_NONE;
                    cfg->uart_valid = true;
                } else if (sbi_strcmp(line, "[cmd]") == 0) {
                    state = PARSE_STATE_CMD;
                } else if (sbi_strcmp(line, "[cmd_end]") == 0) {
                    state = PARSE_STATE_NONE;
                    cfg->cmd_valid = true;
                } else if (sbi_strcmp(line, "[task]") == 0) {
                    state = PARSE_STATE_TASK;
                    cfg->task.start_addr = CONFIG_SRAM_ADDR + line_start_offset;
                    cfg->task_valid = true;
                } else if (sbi_strcmp(line, "[task_end]") == 0) {
                    state = PARSE_STATE_NONE;
                } else {
                    /* key: value */
                    if (state != PARSE_STATE_NONE && state != PARSE_STATE_TASK) {
                        parse_line_by_state(state, line, cfg);
                    }
                }
            }

            line_pos = 0;
            line_start_offset = offset;
            continue;
        }

        if (c != '\r') {
            /*  ignore \r */
            line[line_pos++] = c;
        }
    }

    /* handle last line */
    if (line_pos > 0) {
        line[line_pos] = '\0';
        trim_line(line);
        if (line[0] != '\0' && line[0] != '#' &&
            state != PARSE_STATE_NONE && state != PARSE_STATE_TASK) {
            parse_line_by_state(state, line, cfg);
        }
    }

    return 0;
}

static int make_reg_prop(void *fdt, u64 addr, u64 size, void *out, int out_size)
{
    const fdt32_t *ac, *sc;
    int ac_val = 2, sc_val = 2;
    int len;

    ac = fdt_getprop(fdt, 0, "#address-cells", NULL);
    sc = fdt_getprop(fdt, 0, "#size-cells", NULL);
    if (ac) ac_val = fdt32_to_cpu(*ac);
    if (sc) sc_val = fdt32_to_cpu(*sc);

    len = (ac_val + sc_val) * sizeof(fdt32_t);
    if (out_size < len) return -1;

    fdt32_t *p = (fdt32_t *)out;
    int i;

    /* write address */
    for (i = 0; i < ac_val; i++) {
        p[i] = cpu_to_fdt32((u32)(addr >> (32 * (ac_val - 1 - i))));
    }
    /* write length */
    for (i = 0; i < sc_val; i++) {
        p[ac_val + i] = cpu_to_fdt32((u32)(size >> (32 * (sc_val - 1 - i))));
    }

    return len;
}

/* modify /memory node */
static void patch_memory_node(void *fdt, u64 start, u64 size)
{
    int node = fdt_path_offset(fdt, "/memory");
    if (node < 0) {
        sbi_printf("FDT: /memory node not found\n");
        return;
    }

    fdt32_t reg[4];                 /* support #address-cells=2, #size-cells=2 */
    int len = make_reg_prop(fdt, start, size, reg, sizeof(reg));
    if (len <= 0) {
        sbi_printf("FDT: failed to build memory reg\n");
        return;
    }

    int err = fdt_setprop(fdt, node, "reg", reg, len);
    if (err < 0) {
        sbi_printf("FDT: failed to set /memory reg: %d\n", err);
    } else {
        sbi_printf("FDT: patched /memory reg = 0x%lx@0x%lx\n", size, start);
    }
}

/* modify UART node */
static void patch_uart_node(void *fdt, struct uart_config *uart)
{
    int node_uart0 = -1;
    int node_uart1 = -1;
    const char *target_compat;
    char uart0_path[256];
    char uart1_path[256];
    int coff = -1, len;
    const void *prop;

    sbi_memset(uart0_path, 0, sizeof(uart0_path));
    sbi_memset(uart1_path, 0, sizeof(uart1_path));
    fdt_get_path(fdt, node_uart0, uart0_path, sizeof(uart0_path));
    fdt_get_path(fdt, node_uart1, uart1_path, sizeof(uart1_path));

    if (!fdt || !uart || uart->compatible[0] == '\0') {
        sbi_printf("FDT: invalid UART config\n");
        return;
    }

    if (sbi_strcmp(uart->compatible, UART0_NS16550) != 0 && sbi_strcmp(uart->compatible, UART1_XLNX) != 0) {
        sbi_printf("FDT: invalid UART config\n");
        return;
    }

    target_compat = uart->compatible;

    /* Traverse all device nodes and find the uart0 node that matches the compatible property */
    node_uart0 = fdt_node_offset_by_compatible(fdt, -1, UART0_NS16550);
    sbi_printf("FDT: node_uart0=0x%x\n", node_uart0);
    if (node_uart0 < 0) {
        sbi_printf("FDT: no UART0 node found with compatible '%s'\n", target_compat);
    } else {
        if (sbi_strcmp(uart->compatible, UART0_NS16550) == 0) {
            /* Change the status from 'disabled' to 'okay' */
            int err = fdt_setprop_string(fdt, node_uart0, "status", "okay");
            if (err < 0) {
                sbi_printf("FDT: failed to set status=okay for UART0 '%s': %d\n", target_compat, err);
            }

            coff = fdt_path_offset(fdt, "/chosen");
            if (-1 < coff) {
                prop = fdt_getprop(fdt, coff, "stdout-path", &len);
                if (prop && len) {
                    err = fdt_setprop_string(fdt, coff, "stdout-path", "serial0:115200n8");
                    if (err < 0) {
                        sbi_printf("FDT: failed to set stdout-path for UART0: %d\n", err);
                    }
                }
            } else {
                sbi_printf("FDT: no /chosen node found with compatible\n");
            }
        } else {
            /* Change the status from 'okay' to 'disabled' */
            int err = fdt_setprop_string(fdt, node_uart0, "status", "disabled");
            if (err < 0) {
                sbi_printf("FDT: failed to set status=okay for UART0 '%s': %d\n", target_compat, err);
            }
        }
    }

    node_uart1 = fdt_node_offset_by_compatible(fdt, -1, UART1_XLNX);
    if (node_uart1 < 0) {
        sbi_printf("FDT: no UART1 node found with compatible '%s'\n", target_compat);
    } else {
        if (sbi_strcmp(uart->compatible, UART1_XLNX) == 0) {
            /* Change the status from 'disabled' to 'okay' */
            int err = fdt_setprop_string(fdt, node_uart1, "status", "okay");
            if (err < 0) {
                sbi_printf("FDT: failed to set status=okay for UART1 '%s': %d\n", target_compat, err);
            }

            coff = fdt_path_offset(fdt, "/chosen");
            if (-1 < coff) {
                prop = fdt_getprop(fdt, coff, "stdout-path", &len);
                if (prop && len) {
                    err = fdt_setprop_string(fdt, coff, "stdout-path", "serial1:115200n8");
                    if (err < 0) {
                        sbi_printf("FDT: failed to set stdout-path for UART1: %d\n", err);
                    }
                }
            } else {
                sbi_printf("FDT: no /chosen node found with compatible\n");
            }
        } else {
            /* Change the status from 'okay' to 'disabled' */
            int err = fdt_setprop_string(fdt, node_uart1, "status", "disabled");
            if (err < 0) {
                sbi_printf("FDT: failed to set status=okay for UART1 '%s': %d\n", target_compat, err);
            }
        }
    }

    sbi_printf("FDT: enabled UART node with compatible '%s'\n", target_compat);
}

static char *my_strstr(const char *haystack, const char *needle)
{
    if (!haystack || !needle)
        return NULL;
    if (*needle == '\0')
        return (char *)haystack;

    while (*haystack) {
        const char *h = haystack;
        const char *n = needle;
        while (*h && *n && *h == *n) {
            h++; n++;
        }
        if (*n == '\0')
            return (char *)haystack;
        haystack++;
    }
    return NULL;
}

static int replace_bootarg_with_addr(void *fdt, const char *base_args,
                                     unsigned long start_addr)
{
    const char *key = "task";
    char new_value[32];

    if (!fdt)
        return -1;

    int chosen = fdt_path_offset(fdt, "/chosen");
    if (chosen < 0)
        return chosen;

    if (!start_addr) {
        if (!base_args)
            return 0;
        return fdt_setprop_string(fdt, chosen, "bootargs", base_args);
    }

    sbi_snprintf(new_value, sizeof(new_value), "0x%lx", start_addr);
    const char *old_args = base_args;
    if (!old_args)
        old_args = fdt_getprop(fdt, chosen, "bootargs", NULL);
    if (!old_args)
        old_args = "";

    /* Construct the 'key=' string */
    char key_eq[64];
    u32 key_len = sbi_strlen(key);
    if (key_len + 2 > sizeof(key_eq))       // +1 for '=', +1 for '\0'
        return -1;
    sbi_strncpy(key_eq, key, sizeof(key_eq));
    key_eq[key_len] = '=';
    key_eq[key_len + 1] = '\0';

    char new_args[MAX_BOOTARGS_LEN];
    const char *found = my_strstr(old_args, key_eq);

    if (found) {
        /* find key=xxx */
        u32 prefix_len = found - old_args;

        /* kip past 'key=' and find the end of the value (space or '\0'). */
        const char *val_start = found + sbi_strlen(key_eq);
        const char *val_end = val_start;
        while (*val_end && *val_end != ' ')
            val_end++;

        u32 suffix_len = sbi_strlen(val_end);       /* Including the trailing spaces and parameters. */

        /* Construct a new string: prefix + key=new_value + suffix */
        if (prefix_len + sbi_strlen(key_eq) + sbi_strlen(new_value) + suffix_len >= MAX_BOOTARGS_LEN)
            return -1;      /* Insufficient buffer space */

        /* Copy the prefix */
        sbi_memcpy(new_args, old_args, prefix_len);
        new_args[prefix_len] = '\0';

        /* Concatenate key=new_value */
        sbi_strncpy(new_args + prefix_len, key_eq, MAX_BOOTARGS_LEN - prefix_len);
        u32 pos = prefix_len + sbi_strlen(key_eq);
        sbi_strncpy(new_args + pos, new_value, MAX_BOOTARGS_LEN - pos - 1);
        new_args[MAX_BOOTARGS_LEN - 1] = '\0';

        /* Append the suffix (where val_end points to the content after the old value) */
        u32 current_len = sbi_strlen(new_args);
        if (current_len < MAX_BOOTARGS_LEN - 1) {
            sbi_strncpy(new_args + current_len, val_end, MAX_BOOTARGS_LEN - current_len - 1);
        }
    } else {
        /* If not found, append to the end */
        u32 old_len = sbi_strlen(old_args);
        u32 needed = old_len + (old_len ? 1 : 0) + sbi_strlen(key_eq) + sbi_strlen(new_value);
        if (needed >= MAX_BOOTARGS_LEN)
            return -1;

        if (old_len > 0) {
            sbi_snprintf(new_args, MAX_BOOTARGS_LEN, "%s %s%s", old_args, key_eq, new_value);
        } else {
            sbi_snprintf(new_args, MAX_BOOTARGS_LEN, "%s%s", key_eq, new_value);
        }
    }

    return fdt_setprop_string(fdt, chosen, "bootargs", new_args);
}

static int patch_bootargs_and_task_node(void *fdt, struct platform_config *cfg)
{
    unsigned long start_addr;
    const char *base_args;

    if (!fdt || !cfg)
        return SBI_EINVAL;

    base_args = cfg->cmd.bootargs[0] ? cfg->cmd.bootargs : NULL;
    start_addr = cfg->cmd.start_addr;
    if (!start_addr && cfg->task_valid)
        start_addr = cfg->task.start_addr;

    replace_bootarg_with_addr(fdt, base_args, start_addr);

    return 0;
}

void fdt_modify(void *fdt, struct platform_config *cfg)
{
    static int done = 0;

    if (!done) {
        if (cfg->uart_valid) {
            sbi_printf("cfg->uart_valid is true\n");
            patch_uart_node(fdt, &cfg->uart);
        }

        if (cfg->mem_valid) {
            patch_memory_node(fdt, cfg->mem.start_addr, cfg->mem.size);
        }

        if (cfg->cmd_valid) {
            patch_bootargs_and_task_node(fdt, cfg);
        }
        done = 1;
    }
}

void print_string_at_addr(unsigned long addr)
{
    char *p = (char *)addr;
    int i;
    const int MAX_LEN = 4096;
    char buf[MAX_LEN + 1];

    if (!p) {
        sbi_printf("Invalid address\n");
        return;
    }

    for (i = 0; i < MAX_LEN; i++) {
        if (p[i] == '\0') {
            break;
        }
        buf[i] = p[i];
    }
    buf[i] = '\0';

    sbi_printf("String at 0x%lx: \"%s\"\n", addr, buf);
}

static void print_property(const char *name, const void *val, int len)
{
    sbi_printf("        %s", name);

    if (len == 0) {
        sbi_printf(";\n");
        return;
    }

    sbi_printf(" = ");

    bool is_string = true;
    for (int i = 0; i < len; i++) {
        if (((const char *)val)[i] == '\0' && i != len - 1) {
            is_string = false;
            break;
        }
        if (((const char *)val)[i] < 0x20 || ((const char *)val)[i] > 0x7e) {
            if (((const char *)val)[i] != '\0') {
                is_string = false;
                break;
            }
        }
    }

    if (is_string && len > 0 && ((const char *)val)[len - 1] == '\0') {
        sbi_printf("<%s>;\n", (const char *)val);
    } else if (len % 4 == 0) {
        sbi_printf("<");
        for (int i = 0; i < len; i += 4) {
            uint32_t v = fdt32_to_cpu(((fdt32_t *)val)[i / 4]);
            if (i > 0) sbi_printf(" ");
            sbi_printf("0x%x", v);
        }
        sbi_printf(">;\n");
    } else {
        sbi_printf("[");
        for (int i = 0; i < len; i++) {
            if (i > 0) sbi_printf(" ");
            sbi_printf("%02x", ((const uint8_t *)val)[i]);
        }
        sbi_printf("];\n");
    }
}

static void print_node_recursive(void *fdt, int offset, int depth)
{
    const char *name;
    int len;

    name = fdt_get_name(fdt, offset, NULL);
    if (!name) name = "(unknown)";

    /* Print the node name */
    for (int i = 0; i < depth; i++) sbi_printf("  ");
    sbi_printf("%s {\n", name);

    /* Print all properties */
    for (int prop = fdt_first_property_offset(fdt, offset);
         prop >= 0;
         prop = fdt_next_property_offset(fdt, prop)) {

        const struct fdt_property *p = fdt_get_property_by_offset(fdt, prop, &len);
        if (!p) continue;

        for (int i = 0; i < depth + 1; i++) sbi_printf("  ");
        print_property(fdt_string(fdt, fdt32_to_cpu(p->nameoff)), p->data, len);
    }

    /* Recursively traverse child nodes */
    for (int child = fdt_first_subnode(fdt, offset);
         child >= 0;
         child = fdt_next_subnode(fdt, child)) {
        print_node_recursive(fdt, child, depth + 1);
    }

    /* Close the node. */
    for (int i = 0; i < depth; i++) sbi_printf("  ");
    sbi_printf("};\n");
}

void print_full_fdt(void)
{
    void *fdt = (void *)FDT_ADDR;

    if (!fdt || fdt_check_header(fdt) != 0) {
        sbi_printf("print_full_fdt: Invalid FDT at %p\n", fdt);
        return;
    }

    sbi_printf("\n=== Full Device Tree Dump ===\n");
    sbi_printf("FDT size: %d bytes\n\n", fdt_totalsize(fdt));

    int root = fdt_path_offset(fdt, "/");
    if (root < 0) {
        sbi_printf("Root node not found!\n");
        return;
    }

    print_node_recursive(fdt, root, 0);
    sbi_printf("=== End of Device Tree ===\n\n");
}
