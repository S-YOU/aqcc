#include "aqcc.h"

int temp_reg_table;

void init_temp_reg() { temp_reg_table = 0; }

int get_temp_reg()
{
    for (int i = 0; i < 6; i++) {
        if (temp_reg_table & (1 << i)) continue;
        temp_reg_table |= (1 << i);
        return i + 7;  // corresponding to reg_name's index
    }

    error("no more register");
}

int restore_temp_reg(int i) { temp_reg_table &= ~(1 << (i - 7)); }

const char *reg_name(int byte, int i)
{
    const char *lreg[13];
    lreg[0] = "%al";
    lreg[1] = "%dil";
    lreg[2] = "%sil";
    lreg[3] = "%dl";
    lreg[4] = "%cl";
    lreg[5] = "%r8b";
    lreg[6] = "%r9b";
    lreg[7] = "%r10b";
    lreg[8] = "%r11b";
    lreg[9] = "%r12b";
    lreg[10] = "%r13b";
    lreg[11] = "%r14b";
    lreg[12] = "%r15b";
    const char *xreg[13];
    xreg[0] = "%ax";
    xreg[1] = "%di";
    xreg[2] = "%si";
    xreg[3] = "%dx";
    xreg[4] = "%cx";
    xreg[5] = "%r8w";
    xreg[6] = "%r9w";
    xreg[7] = "%r10w";
    xreg[8] = "%r11w";
    xreg[9] = "%r12w";
    xreg[10] = "%r13w";
    xreg[11] = "%r14w";
    xreg[12] = "%r15w";
    const char *ereg[13];
    ereg[0] = "%eax";
    ereg[1] = "%edi";
    ereg[2] = "%esi";
    ereg[3] = "%edx";
    ereg[4] = "%ecx";
    ereg[5] = "%r8d";
    ereg[6] = "%r9d";
    ereg[7] = "%r10d";
    ereg[8] = "%r11d";
    ereg[9] = "%r12d";
    ereg[10] = "%r13d";
    ereg[11] = "%r14d";
    ereg[12] = "%r15d";
    const char *rreg[13];
    rreg[0] = "%rax";
    rreg[1] = "%rdi";
    rreg[2] = "%rsi";
    rreg[3] = "%rdx";
    rreg[4] = "%rcx";
    rreg[5] = "%r8";
    rreg[6] = "%r9";
    rreg[7] = "%r10";
    rreg[8] = "%r11";
    rreg[9] = "%r12";
    rreg[10] = "%r13";
    rreg[11] = "%r14";
    rreg[12] = "%r15";

    assert(0 <= i && i <= 12);

    switch (byte) {
        case 1:
            return lreg[i];
        case 2:
            return xreg[i];
        case 4:
            return ereg[i];
        case 8:
            return rreg[i];
        default:
            assert(0);
    }
}

char byte2suffix(int byte)
{
    switch (byte) {
        case 8:
            return 'q';
        case 4:
            return 'l';
        default:
            assert(0);
    }
}

int lookup_member_offset(Vector *members, char *member)
{
    // O(n)
    int size = vector_size(members);
    for (int i = 0; i < size; i++) {
        StructMember *sm = (StructMember *)vector_get(members, i);
        if ((sm->type->kind == TY_STRUCT || sm->type->kind == TY_UNION) &&
            sm->name == NULL) {
            // nested anonymous struct with no varname.
            // its members can be accessed like parent struct's members.
            int offset = lookup_member_offset(sm->type->members, member);
            if (offset >= 0) return sm->offset + offset;
        }
        else if (strcmp(sm->name, member) == 0) {
            return sm->offset;
        }
    }
    return -1;
}

typedef struct {
    char *continue_label, *break_label;
    int reg_save_area_stack_idx, overflow_arg_area_stack_idx;
    Vector *code;
} CodeEnv;
CodeEnv *codeenv;

#define SAVE_BREAK_CXT char *break_cxt__org_break_label = codeenv->break_label;
#define RESTORE_BREAK_CXT codeenv->break_label = break_cxt__org_break_label;
#define SAVE_CONTINUE_CXT \
    char *continue_cxt__org_continue_label = codeenv->continue_label;
#define RESTORE_CONTINUE_CXT \
    codeenv->continue_label = continue_cxt__org_continue_label;
#define SAVE_VARIADIC_CXT                                \
    int save_variadic_cxt__reg_save_area_stack_idx =     \
            codeenv->reg_save_area_stack_idx,            \
        save_variadic_cxt__overflow_arg_area_stack_idx = \
            codeenv->overflow_arg_area_stack_idx;

#define RESTORE_VARIADIC_CXT                        \
    codeenv->reg_save_area_stack_idx =              \
        save_variadic_cxt__reg_save_area_stack_idx; \
    codeenv->overflow_arg_area_stack_idx =          \
        save_variadic_cxt__overflow_arg_area_stack_idx;

void generate_code_detail(AST *ast);

void init_code_env()
{
    codeenv = (CodeEnv *)safe_malloc(sizeof(CodeEnv));
    codeenv->continue_label = codeenv->break_label = NULL;
    codeenv->reg_save_area_stack_idx = 0;
    codeenv->code = new_vector();
}

void appcode(const char *src, ...)
{
    char buf[256], buf2[256];  // TODO: enough length?
    int i, bufidx;
    va_list args;

    // copy src to buf.
    // replace # to %% in src.
    for (i = 0, bufidx = 0;; i++) {
        if (src[i] == '\0') break;

        if (src[i] == '#') {
            buf[bufidx++] = '%';
            buf[bufidx++] = '%';
            continue;
        }

        buf[bufidx++] = src[i];
    }
    buf[bufidx] = '\0';

    va_start(args, src);
    vsprintf(buf2, buf, args);
    va_end(args);

    vector_push_back(codeenv->code, new_str(buf2));
}

void dump_code(Vector *code, FILE *fh)
{
    for (int i = 0; i < vector_size(code); i++)
        fprintf(fh, "%s\n", (const char *)vector_get(code, i));
}

const char *last_appended_code()
{
    return (const char *)vector_get(codeenv->code,
                                    vector_size(codeenv->code) - 1);
}

void generate_mov_from_memory(int nbytes, const char *src, int dst_reg)
{
    switch (nbytes) {
        case 1:
            appcode("movsbl %s, %s", src, reg_name(4, dst_reg));
            break;

        case 4:
        case 8:
            appcode("mov %s, %s", src, reg_name(nbytes, dst_reg));
            break;

        default:
            assert(0);
    }
}

void generate_mov_mem_reg(int nbytes, int src_reg, int dst_reg)
{
    generate_mov_from_memory(nbytes, format("(%s)", reg_name(8, src_reg)),
                             dst_reg);
}

int generate_register_code_detail(AST *ast)
{
    switch (ast->kind) {
        case AST_INT: {
            int reg = get_temp_reg();
            appcode("mov $%d, %s", ast->ival, reg_name(4, reg));
            return reg;
        }

        case AST_RETURN: {
            if (ast->lhs) {
                int reg = generate_register_code_detail(ast->lhs);
                restore_temp_reg(reg);
                appcode("mov %s, #rax", reg_name(8, reg));
            }
            else {
                appcode("mov $0, #rax");
            }
            appcode("pop #r13");
            appcode("pop #r14");
            appcode("pop #r15");
            appcode("mov #rbp, #rsp");
            appcode("pop #rbp");
            appcode("ret");
            return -1;
        }

        case AST_ADD: {
            int lreg = generate_register_code_detail(ast->lhs),
                rreg = generate_register_code_detail(ast->rhs);

            // int + ptr
            // TODO: shift
            // TODO: long
            if (match_type2(ast->lhs, ast->rhs, TY_INT, TY_PTR)) {
                appcode("cltq");
                appcode("imul $%d, %s", ast->rhs->type->ptr_of->nbytes,
                        reg_name(4, lreg));
            }

            appcode("add %s, %s", reg_name(8, lreg), reg_name(8, rreg));
            restore_temp_reg(lreg);
            return rreg;
        }

        case AST_SUB: {
            int lreg = generate_register_code_detail(ast->lhs),
                rreg = generate_register_code_detail(ast->rhs);

            // ptr - int
            // TODO: shift
            // TODO: long
            if (match_type2(ast->lhs, ast->rhs, TY_PTR, TY_INT)) {
                appcode("cltq");
                appcode("imul $%d, %s", ast->lhs->type->ptr_of->nbytes,
                        reg_name(4, rreg));
            }

            appcode("sub %s, %s", reg_name(8, rreg), reg_name(8, lreg));

            // ptr - ptr
            if (match_type2(ast->lhs, ast->rhs, TY_PTR, TY_PTR))
                // assume pointer size is 8.
                appcode("sar $%d, %s", 2, reg_name(8, lreg));

            restore_temp_reg(rreg);
            return lreg;
        }

        case AST_MUL: {
            int lreg = generate_register_code_detail(ast->lhs),
                rreg = generate_register_code_detail(ast->rhs);
            appcode("imul %s, %s", reg_name(ast->type->nbytes, lreg),
                    reg_name(ast->type->nbytes, rreg));
            restore_temp_reg(lreg);
            return rreg;
        }

        case AST_DIV: {
            int lreg = generate_register_code_detail(ast->lhs),
                rreg = generate_register_code_detail(ast->rhs);
            appcode("mov %s, %s", reg_name(ast->type->nbytes, lreg),
                    reg_name(ast->type->nbytes, 0));
            appcode("cltd");
            appcode("idiv %s", reg_name(ast->type->nbytes, rreg));
            appcode("mov #rax, %s", reg_name(8, rreg));
            restore_temp_reg(lreg);
            return rreg;
        }

        case AST_REM: {
            int lreg = generate_register_code_detail(ast->lhs),
                rreg = generate_register_code_detail(ast->rhs);
            appcode("mov %s, %s", reg_name(ast->type->nbytes, lreg),
                    reg_name(ast->type->nbytes, 0));
            appcode("cltd");
            appcode("idiv %s", reg_name(ast->type->nbytes, rreg));
            appcode("mov #rdx, %s", reg_name(8, rreg));
            restore_temp_reg(lreg);
            return rreg;
        }

        case AST_UNARY_MINUS: {
            int reg = generate_register_code_detail(ast->lhs);
            appcode("neg %s", reg_name(ast->type->nbytes, reg));
            return reg;
        }

        case AST_COMPL: {
            int reg = generate_register_code_detail(ast->lhs);
            appcode("not %s", reg_name(ast->type->nbytes, reg));
            return reg;
        }

        case AST_LSHIFT: {
            int lreg = generate_register_code_detail(ast->lhs),
                rreg = generate_register_code_detail(ast->rhs);
            appcode("mov %s, #cl", reg_name(1, rreg));
            appcode("sal #cl, %s", reg_name(ast->type->nbytes, lreg));
            restore_temp_reg(rreg);
            return lreg;
        }

        case AST_RSHIFT: {
            int lreg = generate_register_code_detail(ast->lhs),
                rreg = generate_register_code_detail(ast->rhs);
            appcode("mov %s, #cl", reg_name(1, rreg));
            appcode("sar #cl, %s", reg_name(ast->type->nbytes, lreg));
            restore_temp_reg(rreg);
            return lreg;
        }

        case AST_LT: {
            int lreg = generate_register_code_detail(ast->lhs),
                rreg = generate_register_code_detail(ast->rhs);
            appcode("cmp %s, %s", reg_name(ast->type->nbytes, rreg),
                    reg_name(ast->type->nbytes, lreg));
            appcode("setl #al");
            appcode("movzb #al, %s", reg_name(ast->type->nbytes, lreg));
            restore_temp_reg(rreg);
            return lreg;
        }

        case AST_LTE: {
            int lreg = generate_register_code_detail(ast->lhs),
                rreg = generate_register_code_detail(ast->rhs);
            appcode("cmp %s, %s", reg_name(ast->type->nbytes, rreg),
                    reg_name(ast->type->nbytes, lreg));
            appcode("setle #al");
            appcode("movzb #al, %s", reg_name(ast->type->nbytes, lreg));
            restore_temp_reg(rreg);
            return lreg;
        }

        case AST_EQ: {
            int lreg = generate_register_code_detail(ast->lhs),
                rreg = generate_register_code_detail(ast->rhs);
            appcode("cmp %s, %s", reg_name(ast->type->nbytes, rreg),
                    reg_name(ast->type->nbytes, lreg));
            appcode("sete #al");
            appcode("movzb #al, %s", reg_name(ast->type->nbytes, lreg));
            restore_temp_reg(rreg);
            return lreg;
        }

        case AST_AND: {
            int lreg = generate_register_code_detail(ast->lhs),
                rreg = generate_register_code_detail(ast->rhs);
            appcode("and %s, %s", reg_name(ast->type->nbytes, lreg),
                    reg_name(ast->type->nbytes, rreg));
            restore_temp_reg(lreg);
            return rreg;
        }

        case AST_XOR: {
            int lreg = generate_register_code_detail(ast->lhs),
                rreg = generate_register_code_detail(ast->rhs);
            appcode("xor %s, %s", reg_name(ast->type->nbytes, lreg),
                    reg_name(ast->type->nbytes, rreg));
            restore_temp_reg(lreg);
            return rreg;
        }

        case AST_OR: {
            int lreg = generate_register_code_detail(ast->lhs),
                rreg = generate_register_code_detail(ast->rhs);
            appcode("or %s, %s", reg_name(ast->type->nbytes, lreg),
                    reg_name(ast->type->nbytes, rreg));
            restore_temp_reg(lreg);
            return rreg;
        }

        case AST_LAND: {
            char *false_label = make_label_string(),
                 *exit_label = make_label_string();
            int lreg = generate_register_code_detail(ast->lhs);
            restore_temp_reg(lreg);
            appcode("cmp $0, %s", reg_name(ast->type->nbytes, lreg));
            appcode("je %s", false_label);
            // don't execute rhs expression if lhs is false.
            int rreg = generate_register_code_detail(ast->rhs);
            char *rreg_name = reg_name(ast->type->nbytes, rreg);
            appcode("cmp $0, %s", rreg_name);
            appcode("je %s", false_label);
            appcode("mov $1, %s", rreg_name);
            appcode("jmp %s", exit_label);
            appcode("%s:", false_label);
            appcode("mov $0, %s", rreg_name);
            appcode("%s:", exit_label);
            return rreg;
        }

        case AST_LOR: {
            char *true_label = make_label_string(),
                 *exit_label = make_label_string();
            int lreg = generate_register_code_detail(ast->lhs);
            restore_temp_reg(lreg);
            appcode("cmp $0, %s", reg_name(ast->type->nbytes, lreg));
            appcode("jne %s", true_label);
            // don't execute rhs expression if lhs is true.
            int rreg = generate_register_code_detail(ast->rhs);
            char *rreg_name = reg_name(ast->type->nbytes, rreg);
            appcode("cmp $0, %s", rreg_name);
            appcode("jne %s", true_label);
            appcode("cmp $0, %s", rreg_name);
            appcode("jmp %s", exit_label);
            appcode("%s:", true_label);
            appcode("mov $1, %s", rreg_name);
            appcode("%s:", exit_label);
            return rreg;
        }

        case AST_FUNCDEF: {
            // allocate stack
            int stack_idx = 0;
            for (int i = ast->params ? max(0, vector_size(ast->params) - 6) : 0;
                 i < vector_size(ast->env->scoped_vars); i++) {
                AST *var = (AST *)(vector_get(ast->env->scoped_vars, i));

                stack_idx -= var->type->nbytes;
                var->stack_idx = stack_idx;
            }

            stack_idx -= (!!ast->is_variadic) * 48;

            // generate code
            appcode(".global %s", ast->fname);
            appcode("%s:", ast->fname);
            appcode("push #rbp");
            appcode("mov #rsp, #rbp");
            appcode("sub $%d, #rsp", roundup(-stack_idx, 16));
            appcode("push #r15");
            appcode("push #r14");
            appcode("push #r13");

            // assign param to localvar
            if (ast->params) {
                for (int i = 0; i < vector_size(ast->params); i++) {
                    AST *var = lookup_var(
                        ast->env, ((AST *)vector_get(ast->params, i))->varname);
                    if (i < 6)
                        appcode("mov %s, %d(#rbp)",
                                reg_name(var->type->nbytes, i + 1),
                                var->stack_idx);
                    else
                        // should avoid return pointer and saved %rbp
                        var->stack_idx = 16 + (i - 6) * 8;
                }
            }

            // place Register Save Area if the function has variadic params.
            if (ast->is_variadic)
                for (int i = 0; i < 6; i++)
                    appcode("mov %s, %d(#rbp)", reg_name(8, i + 1),
                            stack_idx + i * 8);

            // generate body
            SAVE_VARIADIC_CXT;
            codeenv->reg_save_area_stack_idx = stack_idx;
            codeenv->overflow_arg_area_stack_idx =
                ast->params ? max(0, vector_size(ast->params) - 6) * 8 + 16
                            : -1;
            init_temp_reg();
            generate_register_code_detail(ast->body);
            assert(temp_reg_table == 0);
            RESTORE_VARIADIC_CXT;

            // avoid duplicate needless `ret`
            if (strcmp(last_appended_code(), "ret") == 0) return -1;

            if (ast->type->kind != TY_VOID) appcode("mov $0, #rax");
            appcode("pop #r13");
            appcode("pop #r14");
            appcode("pop #r15");
            appcode("mov #rbp, #rsp");
            appcode("pop #rbp");
            appcode("ret");
            return -1;
        }

        case AST_ASSIGN: {
            int lreg = generate_register_code_detail(ast->lhs),
                rreg = generate_register_code_detail(ast->rhs);

            appcode("mov %s, (%s)", reg_name(ast->type->nbytes, rreg),
                    reg_name(8, lreg));
            restore_temp_reg(lreg);
            return rreg;
        }

        case AST_EXPR_LIST: {
            int reg;
            for (int i = 0; i < vector_size(ast->exprs); i++) {
                reg = generate_register_code_detail(
                    (AST *)vector_get(ast->exprs, i));
                if (i != vector_size(ast->exprs) - 1) restore_temp_reg(reg);
            }
            return reg;
        }

        case AST_LVAR: {
            int reg = get_temp_reg();
            char *rname = reg_name(8, reg);
            if (ast->type->is_static || ast->type->is_extern)
                appcode("lea %s(#rip), %s", ast->gen_varname, rname);
            else
                appcode("lea %d(#rbp), %s", ast->stack_idx, rname);
            return reg;
        }

        case AST_GVAR: {
            int reg = get_temp_reg();
            char *rname = reg_name(8, reg);
            appcode("lea %s(#rip), %s", ast->gen_varname, rname);
            return reg;
        }

        case AST_FUNCCALL: {
            appcode("push #r10");
            appcode("push #r11");
            appcode("push #r12");
            for (int i = vector_size(ast->args) - 1; i >= 0; i--) {
                int reg = generate_register_code_detail(
                    (AST *)(vector_get(ast->args, i)));
                appcode("push %s", reg_name(8, reg));
                restore_temp_reg(reg);
            }
            for (int i = 0; i < min(6, vector_size(ast->args)); i++)
                appcode("pop %s", reg_name(8, i + 1));
            appcode("mov $0, #eax");
            appcode("call %s@PLT", ast->fname);
            appcode("add $%d, #rsp", 8 * max(0, vector_size(ast->args) - 6));
            appcode("pop #r12");
            appcode("pop #r11");
            appcode("pop #r10");
            int reg = get_temp_reg();
            appcode("mov #rax, %s", reg_name(8, reg));
            return reg;
        }

        case AST_POSTINC: {
            char suf = byte2suffix(ast->lhs->type->nbytes);

            int lreg = generate_register_code_detail(ast->lhs);
            char *lreg_name = reg_name(8, lreg);
            int reg = get_temp_reg();
            generate_mov_mem_reg(ast->type->nbytes, lreg, reg);

            if (match_type(ast->lhs, TY_PTR))
                appcode("add%c $%d, (%s)", suf, ast->lhs->type->ptr_of->nbytes,
                        lreg_name);
            else
                appcode("inc%c (%s)", suf, lreg_name);

            restore_temp_reg(lreg);
            return reg;
        }

        case AST_PREINC: {
            char suf = byte2suffix(ast->lhs->type->nbytes);

            int lreg = generate_register_code_detail(ast->lhs);
            char *lreg_name = reg_name(8, lreg);
            int reg = get_temp_reg();

            if (match_type(ast->lhs, TY_PTR))
                appcode("add%c $%d, (%s)", suf, ast->lhs->type->ptr_of->nbytes,
                        lreg_name);
            else
                appcode("inc%c (%s)", suf, lreg_name);

            generate_mov_mem_reg(ast->type->nbytes, lreg, reg);

            restore_temp_reg(lreg);
            return reg;
        }

        case AST_POSTDEC: {
            char suf = byte2suffix(ast->lhs->type->nbytes);

            int lreg = generate_register_code_detail(ast->lhs);
            char *lreg_name = reg_name(8, lreg);
            int reg = get_temp_reg();
            generate_mov_mem_reg(ast->type->nbytes, lreg, reg);

            if (match_type(ast->lhs, TY_PTR))
                appcode("sub%c $%d, (%s)", suf, ast->lhs->type->ptr_of->nbytes,
                        lreg_name);
            else
                appcode("dec%c (%s)", suf, lreg_name);

            restore_temp_reg(lreg);
            return reg;
        }

        case AST_PREDEC: {
            char suf = byte2suffix(ast->lhs->type->nbytes);

            int lreg = generate_register_code_detail(ast->lhs);
            char *lreg_name = reg_name(8, lreg);
            int reg = get_temp_reg();

            if (match_type(ast->lhs, TY_PTR))
                appcode("sub%c $%d, (%s)", suf, ast->lhs->type->ptr_of->nbytes,
                        lreg_name);
            else
                appcode("dec%c (%s)", suf, lreg_name);

            generate_mov_mem_reg(ast->type->nbytes, lreg, reg);

            restore_temp_reg(lreg);
            return reg;
        }

        case AST_ADDR:
        case AST_INDIR:
            return generate_register_code_detail(ast->lhs);

        case AST_COND: {
            char *false_label = make_label_string(),
                 *exit_label = make_label_string();

            int cond_reg = generate_register_code_detail(ast->cond);
            restore_temp_reg(cond_reg);
            appcode("mov %s, #rax", reg_name(8, cond_reg));
            appcode("cmp $0, #eax");
            appcode("je %s", false_label);
            int then_reg = generate_register_code_detail(ast->then);
            restore_temp_reg(then_reg);
            appcode("push %s", reg_name(8, then_reg));
            appcode("jmp %s", exit_label);
            appcode("%s:", false_label);
            if (ast->els != NULL) {
                int els_reg = generate_register_code_detail(ast->els);
                restore_temp_reg(els_reg);
                appcode("push %s", reg_name(8, els_reg));
            }
            appcode("%s:", exit_label);
            int reg = get_temp_reg();
            appcode("pop %s", reg_name(8, reg));
            return reg;
        }

        case AST_IF: {
            char *false_label = make_label_string(),
                 *exit_label = make_label_string();

            int cond_reg = generate_register_code_detail(ast->cond);
            restore_temp_reg(cond_reg);
            appcode("mov %s, #rax", reg_name(8, cond_reg));
            appcode("cmp $0, #eax");
            appcode("je %s", false_label);
            generate_register_code_detail(ast->then);
            appcode("jmp %s", exit_label);
            appcode("%s:", false_label);
            if (ast->els != NULL) generate_register_code_detail(ast->els);
            appcode("%s:", exit_label);
            return -1;
        }

        case AST_SWITCH: {
            int target_reg = generate_register_code_detail(ast->target);
            restore_temp_reg(target_reg);
            char *name = reg_name(ast->target->type->nbytes, target_reg);

            for (int i = 0; i < vector_size(ast->cases); i++) {
                SwitchCase *cas = (SwitchCase *)vector_get(ast->cases, i);
                appcode("cmp $%d, %s", cas->cond, name);
                // case has been already labeled when analyzing.
                appcode("je %s", cas->label_name);
            }
            char *exit_label = make_label_string();
            if (ast->default_label)
                appcode("jmp %s", ast->default_label);
            else
                appcode("jmp %s", exit_label);

            SAVE_BREAK_CXT;
            codeenv->break_label = exit_label;
            generate_register_code_detail(ast->switch_body);
            appcode("%s:", exit_label);
            RESTORE_BREAK_CXT;

            return -1;
        }

        case AST_DOWHILE: {
            SAVE_BREAK_CXT;
            SAVE_CONTINUE_CXT;
            codeenv->break_label = make_label_string();
            codeenv->continue_label = make_label_string();
            char *start_label = make_label_string();

            appcode("%s:", start_label);
            generate_register_code_detail(ast->then);
            appcode("%s:", codeenv->continue_label);
            int cond_reg = generate_register_code_detail(ast->cond);
            appcode("cmp $0, %s", reg_name(ast->cond->type->nbytes, cond_reg));
            appcode("jne %s", start_label);
            appcode("%s:", codeenv->break_label);

            restore_temp_reg(cond_reg);
            RESTORE_BREAK_CXT;
            RESTORE_CONTINUE_CXT;

            return -1;
        }

        case AST_FOR: {
            SAVE_BREAK_CXT;
            SAVE_CONTINUE_CXT;
            codeenv->break_label = make_label_string();
            codeenv->continue_label = make_label_string();
            char *start_label = make_label_string();

            if (ast->initer != NULL) {
                int reg = generate_register_code_detail(ast->initer);
                if (reg != -1) restore_temp_reg(reg);  // if expr
            }
            appcode("%s:", start_label);
            if (ast->midcond != NULL) {
                int reg = generate_register_code_detail(ast->midcond);
                appcode("cmp $0, %s",
                        reg_name(ast->midcond->type->nbytes, reg));
                appcode("je %s", codeenv->break_label);
                restore_temp_reg(reg);
            }
            generate_register_code_detail(ast->for_body);
            appcode("%s:", codeenv->continue_label);
            if (ast->iterer != NULL) {
                int reg = generate_register_code_detail(ast->iterer);
                if (reg != -1) restore_temp_reg(reg);  // if nop
            }
            appcode("jmp %s", start_label);
            appcode("%s:", codeenv->break_label);

            RESTORE_BREAK_CXT;
            RESTORE_CONTINUE_CXT;

            return -1;
        }

        case AST_LABEL:
            appcode("%s:", ast->label_name);
            generate_register_code_detail(ast->label_stmt);
            return -1;

        case AST_BREAK:
            appcode("jmp %s", codeenv->break_label);
            return -1;

        case AST_CONTINUE:
            appcode("jmp %s", codeenv->continue_label);
            return -1;

        case AST_GOTO:
            appcode("jmp %s", ast->label_name);
            return -1;

        case AST_MEMBER_REF: {
            int offset =
                lookup_member_offset(ast->stsrc->type->members, ast->member);
            // the member existence is confirmed when analysis.
            assert(offset >= 0);

            int reg = generate_register_code_detail(ast->stsrc);
            appcode("add $%d, %s", offset, reg_name(8, reg));
            return reg;
        }

        case AST_COMPOUND:
            for (int i = 0; i < vector_size(ast->stmts); i++)
                generate_register_code_detail((AST *)vector_get(ast->stmts, i));
            return -1;

        case AST_EXPR_STMT:
            if (ast->lhs != NULL) {
                int reg = generate_register_code_detail(ast->lhs);
                if (reg != -1) restore_temp_reg(reg);
            }
            return -1;

        case AST_ARY2PTR:
            return generate_register_code_detail(ast->ary);

        case AST_CHAR2INT:
            return generate_register_code_detail(ast->lhs);

        case AST_LVALUE2RVALUE: {
            int lreg = generate_register_code_detail(ast->lhs),
                rreg = get_temp_reg();
            generate_mov_mem_reg(ast->type->nbytes, lreg, rreg);
            restore_temp_reg(lreg);
            return rreg;
        }

        case AST_LVAR_DECL_INIT:
        case AST_GVAR_DECL_INIT: {
            generate_register_code_detail(ast->lhs);
            int rreg = generate_register_code_detail(ast->rhs);
            if (rreg != -1) restore_temp_reg(rreg);
            return -1;
        }

        case AST_DECL_LIST:
            for (int i = 0; i < vector_size(ast->decls); i++)
                generate_register_code_detail((AST *)vector_get(ast->decls, i));
            return -1;

        case AST_VA_START: {
            int reg = generate_register_code_detail(ast->lhs);
            char *rname = reg_name(8, reg);
            assert(ast->rhs->kind == AST_INT);
            appcode("movl $%d, (%s)", ast->rhs->ival * 8, rname);
            appcode("movl $48, 4(%s)", rname);
            appcode("leaq %d(#rbp), #rdi", codeenv->overflow_arg_area_stack_idx,
                    rname);
            appcode("mov #rdi, 8(%s)", rname);
            appcode("leaq %d(#rbp), #rdi", codeenv->reg_save_area_stack_idx,
                    rname);
            appcode("mov #rdi, 16(%s)", rname);
            restore_temp_reg(reg);
            return -1;
        }

        case AST_GVAR_DECL:
        case AST_LVAR_DECL:
        case AST_FUNC_DECL:
        case AST_NOP:
            return -1;
    }

    warn("%d\n", ast->kind);
    assert(0);
}

Vector *generate_register_code(Vector *asts)
{
    init_code_env();

    appcode(".text");

    for (int i = 0; i < vector_size(asts); i++)
        generate_register_code_detail((AST *)vector_get(asts, i));

    appcode(".data");

    Vector *gvar_list = get_gvar_list();
    for (int i = 0; i < vector_size(gvar_list); i++) {
        GVar *gvar = (GVar *)vector_get(gvar_list, i);

        appcode("%s:", gvar->name);
        if (gvar->sval) {
            assert(gvar->type->kind == TY_ARY &&
                   gvar->type->ptr_of->kind == TY_CHAR);
            appcode(".ascii \"%s\"",
                    escape_string(gvar->sval, gvar->type->nbytes));
            continue;
        }

        if (gvar->ival == 0) {
            appcode(".zero %d", gvar->type->nbytes);
            continue;
        }

        assert(gvar->type->kind != TY_ARY);  // TODO: implement

        const char *type2spec[16];  // TODO: enough length?
        type2spec[TY_INT] = ".long";
        type2spec[TY_CHAR] = ".byte";
        type2spec[TY_PTR] = ".quad";
        appcode("%s %d", type2spec[gvar->type->kind], gvar->ival);
    }

    return clone_vector(codeenv->code);
}
