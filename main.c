#include <stdio.h>
#include <toml.h>
#include <unistd.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>

#define VERSION "1.2.0"

typedef struct {
    toml_table_t *cfg;
    // the trio
    char *cc;
    char *as;
    char *ld;
} bake_config_t;

typedef struct {
    bool isexec;
    bool islib;
    char *srcs;
    char *bindir;
    toml_array_t *ccflags;
    toml_array_t *incflags;
    toml_array_t *ldflags;
    toml_array_t *deps;
    char *binname;
    char *scrname;
    char *idname;
    bool depcompiled;
    bool cleaned;
} bake_project_t;

typedef struct {
    char bakefile[PATH_MAX];
    toml_table_t *toml;
    char err[512];
    bake_config_t cfg;
    bake_project_t *proj;
    toml_table_t *projlist;
    int projs;
} bake_state_t;

bake_state_t b;

void styl_reset()
{
    printf("\033[0m");
}

void styl_set_color(int col)
{
    printf("\033[38;5;%dm", col);
}
void styl_set_tcol(int r, int g, int b)
{
    printf("\033[38;2;%d;%d;%dm", r, g, b);
}

void styl_set_bold(bool dobold)
{
    if(dobold) {
        printf("\033[1m");
    } else {
        printf("\033[22m");
    }
}

void handlerr()
{
    if(!b.toml) {
        fprintf(stderr, "error to parse toml: %s\n", b.err);
        exit(1);
    }
    if(!b.cfg.cfg) {
        fprintf(stderr, "error to parse toml: %s\n", b.err);
        exit(1);
    }
    if(!b.projlist) {
        fprintf(stderr, "error to parse toml: %s\n", b.err);
        exit(1);
    }
}

void cleanup_proj(bake_project_t p)
{
    free(p.srcs);
    free(p.bindir);
    free(p.binname);
    free(p.scrname);
    free(p.idname);
}

void cleanup_projs()
{
    for(int i = 0; i < b.projs; i++) {
        cleanup_proj(b.proj[i]);
    }
    free(b.proj);
}

void add_proj(bake_project_t proj)
{
    b.projs++;
    b.proj = realloc(b.proj, sizeof(bake_project_t) * b.projs);
    b.proj[b.projs - 1] = proj;
    return;
}

void cleanup()
{
    cleanup_projs();
    free(b.cfg.cc);
    free(b.cfg.as);
    free(b.cfg.ld);
    toml_free(b.toml);
}

void tab()
{
    printf("    ");
}

static int parse_ext(const struct dirent *dir)
{
    if(!dir)
        return 0;

    if(dir->d_type == DT_REG) {
        const char *ext = strrchr(dir->d_name, '.');
        if((!ext) || (ext == dir->d_name))
            return 0;
        else {
            if(strcmp(ext, ".c") == 0)
                return 1;
        }
    }

    return 0;
}

void progressbarprint(int prog)
{
    // GREEN
    // #b9f27c
    // BLUE
    // #7da6ff
    float r0 = (float)(0xb9) / (float)0xff;
    float r1 = (float)(0x7d) / (float)0xff;
    float g0 = (float)(0xf2) / (float)0xff;
    float g1 = (float)(0xa6) / (float)0xff;
    float b0 = (float)(0x7c) / (float)0xff;
    float b1 = (float)(0xff) / (float)0xff;

    const int n = 25;
    int c = prog / (100 / n);
    for(int i = 0; i < c; i++) {
        float t = (float)i / (float)n;
        float R = ((1.0 - t) * r0) + (t * r1);
        float G = ((1.0 - t) * g0) + (t * g1);
        float B = ((1.0 - t) * b0) + (t * b1);
        int ir = (int)(R * 255);
        int ig = (int)(G * 255);
        int ib = (int)(B * 255);
        styl_set_tcol(ir, ig, ib);
        styl_set_bold(true);
        printf("=");
        styl_reset();
    }
    if(c != n) {
        float t = (float)c / (float)n;
        float R = ((1.0 - t) * r0) + (t * r1);
        float G = ((1.0 - t) * g0) + (t * g1);
        float B = ((1.0 - t) * b0) + (t * b1);
        int ir = (int)(R * 255);
        int ig = (int)(G * 255);
        int ib = (int)(B * 255);
        styl_set_tcol(ir, ig, ib);
        styl_set_bold(true);
        printf(">");
        styl_reset();
    }
    c += (c != n);
    c = n - c;
    for(int i = 0; i < c; i++) {
        printf(" ");
    }
}

void _add_argv(int argcnt, char ***argv, char *toadd)
{
    *argv = realloc(*argv, sizeof(char *) * (argcnt + 1));
    char **local = *argv;
    local[argcnt] = strdup(toadd);
}
#define add_argv(argcnt, argv, toadd)   \
    {                                   \
        _add_argv(argcnt, argv, toadd); \
        argcnt++;                       \
    }

void compile_exec(bake_project_t p, char *name, int argc, char *argv[])
{
    int stat, exitcode;
    int forked_pid = fork();
    if(forked_pid == 0) {
        execvp(argv[0], argv);
        exit(EXIT_SUCCESS);
    } else {
        waitpid(forked_pid, &stat, 0);
        if(WIFSIGNALED(stat)) {
            fprintf(
                stderr,
                "error: compiler expierenced error that is not related to the compilation\n");
            exit(1);
        }
        exitcode = WEXITSTATUS(stat);
        if(exitcode) {
            fprintf(stderr, "error: compiler errored, terminating bake\n");
            exit(1);
        }
    }
    return;
}

void linkapp(bake_project_t p, struct dirent **list, int bn)
{
    tab();
    styl_set_bold(true);
    styl_set_color(5);
    printf("Linking ");
    styl_reset();
    printf("%s\n", p.scrname);
    int argc = 0;
    char **argv = malloc(1);
    add_argv(argc, &argv, b.cfg.ld);
    int ccflag_cnt, incflag_cnt, ldflag_cnt;
    ccflag_cnt = toml_array_nelem(p.ccflags);
    incflag_cnt = toml_array_nelem(p.incflags);
    ldflag_cnt = toml_array_nelem(p.ldflags);

    for(int i = 0; i < incflag_cnt; i++) {
        add_argv(argc, &argv, toml_string_at(p.incflags, i).u.s);
    }
    for(int i = 0; i < ccflag_cnt; i++) {
        add_argv(argc, &argv, toml_string_at(p.ccflags, i).u.s);
    }
    if(ldflag_cnt != 1 && strcmp(toml_string_at(p.ldflags, 0).u.s, "") != 0) {
        for(int i = 0; i < ldflag_cnt; i++) {
            add_argv(argc, &argv, toml_string_at(p.ldflags, i).u.s);
        }
    }
    char *nm = malloc(PATH_MAX);
    for(int i = 0; i < bn; i++) {
        char *freeme = strdup(list[i]->d_name);
        memset(nm, 0, PATH_MAX);
        strlcpy(nm, p.srcs, PATH_MAX);
        strlcat(nm, "/", PATH_MAX);
        strlcat(nm, freeme, PATH_MAX);
        char *og = freeme;
        freeme = strdup(nm);
        freeme[strlen(freeme) - 1] = 'o';
        add_argv(argc, &argv, freeme);
        free(freeme);
        free(og);
    }
    free(nm);
    char *freeme = strdup("-o");
    char *freeme1 = malloc(PATH_MAX);
    strlcpy(freeme1, p.bindir, PATH_MAX);
    strlcat(freeme1, "/", PATH_MAX);
    strlcat(freeme1, p.binname, PATH_MAX);
    add_argv(argc, &argv, freeme);
    add_argv(argc, &argv, freeme1);
    free(freeme);
    free(freeme1);
    compile_exec(p, NULL, argc, argv);
}

void linklib(bake_project_t p, struct dirent **list, int bn)
{
    tab();
    styl_set_bold(true);
    styl_set_color(5);
    printf("Linking ");
    styl_reset();
    printf("%s\n", p.scrname);
    int argc = 0;
    char **argv = malloc(1);
    char *arcmd = strdup("ar");
    add_argv(argc, &argv, arcmd);

    char *freeme = strdup("rcs");
    char *freeme1 = malloc(PATH_MAX);
    strlcpy(freeme1, p.bindir, PATH_MAX);
    strlcat(freeme1, "/", PATH_MAX);
    strlcat(freeme1, p.binname, PATH_MAX);
    add_argv(argc, &argv, freeme);
    add_argv(argc, &argv, freeme1);
    free(freeme);
    free(freeme1);
    char *nm = malloc(PATH_MAX);
    for(int i = 0; i < bn; i++) {
        char *freeme = strdup(list[i]->d_name);
        memset(nm, 0, PATH_MAX);
        strlcpy(nm, p.srcs, PATH_MAX);
        strlcat(nm, "/", PATH_MAX);
        strlcat(nm, freeme, PATH_MAX);
        char *og = freeme;
        freeme = strdup(nm);
        freeme[strlen(freeme) - 1] = 'o';
        add_argv(argc, &argv, freeme);
        free(freeme);
        free(og);
    }
    free(nm);

    compile_exec(p, NULL, argc, argv);
    free(arcmd);
}

void compile(bake_project_t p, char *name, int i, int n)
{
    printf("\r");
    tab();
    styl_set_bold(true);
    styl_set_color(4);
    printf("Compiling ");
    styl_reset();
    //printf("%s\n", name);
    styl_set_color(2);
    printf("[");
    styl_reset();
    float p_ = (float)i / (float)n;
    p_ *= 100;
    progressbarprint((int)p_);
    styl_set_color(4);
    printf("]");
    styl_reset();
    styl_set_bold(true);
    printf(" %d/%d", i, n);
    styl_set_bold(false);
    printf(" %s                   ", name);

    int argc = 0;
    char **argv = malloc(1);
    add_argv(argc, &argv, b.cfg.cc);
    int ccflag_cnt, incflag_cnt;
    ccflag_cnt = toml_array_nelem(p.ccflags);
    incflag_cnt = toml_array_nelem(p.incflags);
    for(int i = 0; i < ccflag_cnt; i++) {
        add_argv(argc, &argv, toml_string_at(p.ccflags, i).u.s);
    }
    for(int i = 0; i < incflag_cnt; i++) {
        add_argv(argc, &argv, toml_string_at(p.incflags, i).u.s);
    }
    char *nm = malloc(PATH_MAX);
    strlcpy(nm, p.srcs, PATH_MAX);
    strlcat(nm, "/", PATH_MAX);
    strlcat(nm, name, PATH_MAX);
    char *freeme1 = strdup("-o");
    char *freeme2 = strdup("-c");
    char *freeme3 = strdup(nm);
    int freeme3_indx = strlen(freeme3) - 1;
    freeme3[freeme3_indx] = 'o';
    add_argv(argc, &argv, freeme1);
    add_argv(argc, &argv, freeme3);
    add_argv(argc, &argv, freeme2);
    add_argv(argc, &argv, nm);
    free(freeme1);
    free(freeme2);
    free(freeme3);
    /*printf("Arguments: %d\n", argc);
    printf("Argument list:\n");
    for(int i = 0; i < argc; i++) {
        printf("\t[%d] %s\n", i, argv[i]);
    }*/
    compile_exec(p, name, argc, argv);
    free(nm);
    for(int i = 0; i < argc; i++) {
        free(argv[i]);
    }
}

void compilecleanup(bake_project_t p)
{
    int ccflag_cnt, incflag_cnt, ldflag_cnt, depcnt;
    ccflag_cnt = toml_array_nelem(p.ccflags);
    incflag_cnt = toml_array_nelem(p.incflags);
    ldflag_cnt = toml_array_nelem(p.ldflags);
    depcnt = toml_array_nelem(p.deps);
    for(int i = 0; i < ccflag_cnt; i++) {
        free(toml_string_at(p.ccflags, i).u.s);
    }
    for(int i = 0; i < incflag_cnt; i++) {
        free(toml_string_at(p.incflags, i).u.s);
    }
    for(int i = 0; i < ldflag_cnt; i++) {
        free(toml_string_at(p.ldflags, i).u.s);
    }
    for(int i = 0; i < depcnt; i++) {
        free(toml_string_at(p.deps, i).u.s);
    }
}

void build_project(bake_project_t p)
{
    if(p.depcompiled) {
        if(!p.cleaned) {
            p.cleaned = true;
            compilecleanup(p);
        }
        return;
    } else {
        int depcount = toml_array_nelem(p.deps);
        if(depcount) {
            for(int i = 0; i < depcount; i++) {
                toml_datum_t depnam = toml_string_at(p.deps, i);

                // search for the dependice
                int dep_indx = -1;
                for(int i = 0; i < b.projs; i++) {
                    if(strcmp(b.proj[i].idname, depnam.u.s) == 0) {
                        dep_indx = i;
                    }
                }
                if(dep_indx == -1) {
                    fprintf(stderr, "error: dependicy '%s' not found\n",
                            depnam.u.s);
                }
                styl_reset();
                styl_set_bold(true);
                styl_set_color(3);
                tab();
                printf("Fetching");
                styl_reset();
                printf(" %s\n", b.proj[dep_indx].scrname);
                //printf("dbg: compiling dep '%s'\n", depnam.u.s);
                build_project(b.proj[dep_indx]);
                b.proj[dep_indx].depcompiled = true;
            }
        } else
            p.depcompiled = true;
    }
    struct dirent **list;
    int n, bn, i;
    i = 0;
    n = scandir(p.srcs, &list, parse_ext, alphasort);
    bn = n;
    if(n < 0) {
        fprintf(stderr, "error: program error: scandir\nperror log: \n");
        perror("scandir");
        exit(1);
    }
    while(n--) {
        i++;
        compile(p, list[n]->d_name, i, bn);
    }
    printf("\n");
    if(p.isexec)
        linkapp(p, list, bn);
    if(p.islib)
        linklib(p, list, bn);
    compilecleanup(p);
    free(list);
}

bake_project_t parse_proj_toml(toml_table_t *projroot, char *target,
                               char *target_scrname)
{
    toml_table_t *proj = toml_table_in(projroot, target);
    if(!proj) {
        fprintf(stderr, "error: cannot find [project.%s]\n", target);
        exit(1);
    }
    bake_project_t ret;
    ret.isexec = false;
    ret.islib = false;
    toml_datum_t typ = toml_string_in(proj, "type");
    if(strcmp(typ.u.s, "exec") == 0) {
        ret.isexec = true;
        ret.islib = false;
    }
    if(strcmp(typ.u.s, "lib") == 0) {
        if(ret.isexec && !ret.islib) {
            fprintf(stderr, "What? How?\n");
            exit(1);
        }
        ret.isexec = false;
        ret.islib = true;
    }
    free(typ.u.s);
    toml_datum_t srcs = toml_string_in(proj, "srcs");
    toml_datum_t bin = toml_string_in(proj, "bin");
    toml_array_t *ccflags = toml_array_in(proj, "ccflags");
    toml_array_t *incflags = toml_array_in(proj, "incflags");
    toml_array_t *ldflags = toml_array_in(proj, "ldflags");
    toml_array_t *deps = toml_array_in(proj, "deps");
    toml_datum_t binname = toml_string_in(proj, "binname");
    ret.srcs = srcs.u.s;
    ret.binname = binname.u.s;
    ret.bindir = bin.u.s;
    ret.ccflags = ccflags;
    ret.idname = strdup(target);
    ret.scrname = strdup(target_scrname);
    ret.incflags = incflags;
    ret.ldflags = ldflags;
    ret.deps = deps;
    ret.depcompiled = false;
    ret.cleaned = false;
    return ret;
}

int main(int argc, char *argv[])
{
    styl_set_bold(true);
    styl_set_color(1);
    tab();
    printf("Bake ");
    styl_reset();
    printf(" %s\n", VERSION);
    if(argc > 2) {
        printf("error: excessive arguments\nhelp: %s [optional: bake file]\n",
               argv[0]);
    }
    if(argc == 1) {
        strlcpy(b.bakefile, "bake.toml", PATH_MAX);
    } else {
        strlcpy(b.bakefile, argv[1], PATH_MAX);
    }
    b.cfg.cfg = (void *)1;
    b.toml = (void *)1;
    b.projlist = (void *)1;
    b.proj = malloc(1);
    tab();
    styl_set_bold(true);
    styl_set_color(3);
    printf("Using ");
    styl_reset();
    printf("bakefile: %s\n", b.bakefile);
    FILE *f = fopen(b.bakefile, "r");
    b.toml = toml_parse_file(f, b.err, 512);
    fclose(f);
    handlerr();
    //printf("parsed toml file\n");
    b.cfg.cfg = toml_table_in(b.toml, "config");
    handlerr();
    toml_datum_t cfg_cc = toml_string_in(b.cfg.cfg, "cc");
    toml_datum_t cfg_as = toml_string_in(b.cfg.cfg, "as");
    toml_datum_t cfg_ld = toml_string_in(b.cfg.cfg, "ld");
    if(!cfg_cc.ok || !cfg_as.ok || !cfg_ld.ok) {
        fprintf(stderr, "error: cannot read configuration\n");
    }
    b.cfg.cc = cfg_cc.u.s;
    b.cfg.as = cfg_as.u.s;
    b.cfg.ld = cfg_ld.u.s;
    /*
    printf("compilation configuration loaded,\n");
    printf("\tc compiler: %s\n", b.cfg.cc);
    printf("\tc assembler: %s\n", b.cfg.as);
    printf("\tc linker: %s\n", b.cfg.ld);*/

    b.projlist = toml_table_in(b.toml, "project");
    handlerr();
    //printf("reading projects\n");
    toml_array_t *projarr = toml_array_in(b.projlist, "sub");
    if(!projarr) {
        fprintf(stderr, "error: cannot read project.sub\n");
        exit(1);
    }
    for(int i = 0;; i++) {
        toml_array_t *projprop = toml_array_at(projarr, i);
        if(!projprop) {
            //printf("end of project list\n");
            break;
        }
        toml_datum_t scrname = toml_string_at(projprop, 0);
        if(!scrname.ok) {
            fprintf(stderr, "error: cannot read project list\n");
            exit(1);
        }
        toml_datum_t idname = toml_string_at(projprop, 1);
        if(!idname.ok) {
            fprintf(stderr, "error: cannot read project list\n");
            exit(1);
        }
        //printf("found project %s (id: %s), adding it to project list\n",
        //       scrname.u.s, idname.u.s);
        bake_project_t p = parse_proj_toml(b.projlist, idname.u.s, scrname.u.s);
        //printf("parsed project %s (id: %s) toml\n", scrname.u.s, idname.u.s);
        add_proj(p);
        free(idname.u.s);
        free(scrname.u.s);
    }
    for(int i = 0; i < b.projs; i++) {
        bool f = b.proj[i].depcompiled;
        if(!f) {
            tab();
            styl_set_bold(true);
            styl_set_color(2);
            printf("Building ");
            styl_reset();
            printf("%s\n", b.proj[i].scrname);
        }
        build_project(b.proj[i]);
        if(!f) {
            tab();
            styl_set_bold(true);
            styl_set_color(27);
            printf("Finished ");
            styl_reset();
            printf("%s\n", b.proj[i].scrname);
        }
    }

    cleanup();
    return 0;
}
