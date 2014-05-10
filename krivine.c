#include <ctype.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <arpa/inet.h>
#include "krivine-common.c"
#include "ccnl-ext-debug.c"
#include "ccnl-includes.h"


#define LAMBDA '@'

#define term_is_var(t)     (!(t)->m)
#define term_is_app(t)     ((t)->m && (t)->n)
#define term_is_lambda(t)  (!(t)->n)

//Machine state functions
//------------------------------------------------------------------
struct stack_s{
    void *content;
    struct stack_s *next;
};

struct environment_s{
    char *name;
    void *element;
    struct environment_s *next;
};

struct closure_s{
    char *term;
    struct environment_s *env;
};

struct closure_s *
new_closure(char *term, struct environment_s *env){
    struct closure_s *ret = malloc(sizeof(struct closure_s));
    ret->term = term;
    ret->env = env;
    return ret;
}

void 
push_to_stack(struct stack_s **top, void *content){
    struct stack_s *h = malloc(sizeof(struct stack_s));
    if(top != NULL && *top != NULL){
                h->next = *top;
        }else{
	        h->next = NULL;
	    }
    h->content = content;
    *top = h;
}

void *
pop_from_stack(struct stack_s **top){
    void *res;
    if(top == NULL) return NULL;
    if( *top != NULL){
        struct stack_s *h = *top;
        res = h->content;
    	*top = h->next;
        free(h);
        return res;
    }
    return NULL;
}

char *
pop_or_resolve_from_result_stack(struct ccnl_relay_s *ccnl, struct configuration_s *config,
        int *restart){
    char *res = NULL;
    /*if(*restart){
        DEBUGMSG(99, "pop_or_resolve_from_result_stack: Check for content: %s \n", config->fox_state->thunk);
        res = config->fox_state->thunk;
    } 
    else{*/
    res = (char*) pop_from_stack(&config->result_stack);
    //}
    struct ccnl_content_s *c;
    if(res && !strncmp(res, "THUNK", 5)){
        DEBUGMSG(49, "Resolve Thunk: %s \n", res);
        if(!config->thunk){
            config->thunk = 1;
            config->starttime = CCNL_NOW();
            config->endtime = config->starttime+config->thunk_time; //TODO: determine number from interest
            DEBUGMSG(99,"Wating %f sec for Thunk\n", config->thunk_time);
        }
        c = ccnl_nfn_resolve_thunk(ccnl, config, res);
        if(c == NULL){
            push_to_stack(&config->result_stack, res);
            return NULL;
        }
        config->thunk = 0;
        res = c->content;  
    }
    return res;
}

void 
print_environment(struct environment_s *env){
   struct environment_s *e;
   printf("Environment address is: %p\n",env);
   int num = 0;
   for(e = env; e; e = e->next){
	printf("Element %d %s %p\n", num++, e->name, e->element);        
   }
}

void 
print_result_stack(struct stack_s *stack){
   struct stack_s *s;
   int num = 0;
   for(s = stack; s; s = s->next){
       printf("Element %d %s\n", num++, s->content);
   }
}

void 
print_argument_stack(struct stack_s *stack){
   struct stack_s *s;
   int num = 0;
   for(s = stack; s; s = s->next){
        struct closure_s *c = s->content;
	printf("Element %d %s\n ---Env: \n", num++, c->term);  
	print_environment(c->env);
	printf("--End Env\n");      
   }
}

void 
add_to_environment(struct environment_s **env, char *name, void *element){
   struct environment_s *newelement = malloc(sizeof(struct environment_s));
   newelement->name = name;
   newelement->element = element;
   if(env == NULL || *env == NULL){
          newelement->next = NULL;
      }else{
             newelement->next = *env;
         }
   *env = newelement;
}

void *
search_in_environment(struct environment_s *env, char *name){
  struct environment_s *envelement;
  for(envelement = env; envelement; envelement = envelement->next){
    	if(!strcmp(name, envelement->name)){
		     return envelement->element;	
		}
    }
  return NULL;
}


//parse functions
//------------------------------------------------------------------

struct term_s {
    char *v;
    struct term_s *m, *n;
    // if m is 0, we have a var  v
    // is both m and n are not 0, we have an application  (M N)
    // if n is 0, we have a lambda term  /v M
};

char*
parse_var(char **cpp)
{
    char *p;
    int len;

    p = *cpp;
    while (*p && (isalnum(*p) || *p == '_' || *p == '=' || *p == '/'))
	p++;
    len = p - *cpp;
    p = malloc(len+1);
    if (!p)
	return 0;
    memcpy(p, *cpp, len);
    p[len] = '\0';
    *cpp += len;
    return p;
}

struct term_s*
parseKRIVINE(int lev, char **cp)
{
/* t = (v, m, n)

   var:     v!=0, m=0, n=0
   app:     v=0, m=f, n=arg
   lambda:  v!=0, m=body, n=0
 */
    struct term_s *t = 0, *s, *u;

    while (**cp) {
	while (isspace(**cp))
	    *cp += 1;

//	printf("parseKRIVINE %d %s\n", lev, *cp);

	if (**cp == ')')
	    return t;
	if (**cp == '(') {
	    *cp += 1;
	    s = parseKRIVINE(lev+1, cp);
	    if (!s)
		return 0;
	    if (**cp != ')') {
		printf("parseKRIVINE error: missing )\n");
		return 0;
	    } else
		*cp += 1;
	} else if (**cp == LAMBDA) {
	    *cp += 1;
	    s = calloc(1, sizeof(*s));
	    s->v = parse_var(cp);
	    s->m = parseKRIVINE(lev+1, cp);
//	    printKRIVINE(dummybuf, s->m, 0);
//	    printf("  after lambda: /%s %s --> <%s>\n", s->v, dummybuf, *cp);
	} else {
	    s = calloc(1, sizeof(*s));
	    s->v = parse_var(cp);
//	    printf("  var: <%s>\n", s->v);
	}
	if (t) {
//	    printKRIVINE(dummybuf, t, 0);
//	    printf("  old term: <%s>\n", dummybuf);
	    u = calloc(1, sizeof(*u));
	    u->m = t;
	    u->n = s;
	    t = u;
	} else
	    t = s;
//	printKRIVINE(dummybuf, t, 0);
//	printf("  new term: <%s>\n", dummybuf);
    }
//    printKRIVINE(dummybuf, t, 0);
//    printf("  we return <%s>\n", dummybuf);
    return t;
}

int
printKRIVINE(char *cfg, struct term_s *t, char last)
{
    int len = 0;

    if (t->v && t->m) { // Lambda (sequence)
	len += sprintf(cfg + len, "(%c%s", LAMBDA, t->v);
	len += printKRIVINE(cfg + len, t->m, 'a');
	len += sprintf(cfg + len, ")");
	return len;
    }
    if (t->v) { // (single) variable
	if (isalnum(last))
	    len += sprintf(cfg + len, " %s", t->v);
	else
	    len += sprintf(cfg + len, "%s", t->v);
	return len;
    }
    // application (sequence)
#ifdef CORRECT_PARENTHESES
    len += sprintf(cfg + len, "(");
    len += printKRIVINE(cfg + len, t->m, '(');
    len += printKRIVINE(cfg + len, t->n, 'a');
    len += sprintf(cfg + len, ")");
#else
    if (t->n->v && !t->n->m) {
	len += printKRIVINE(cfg + len, t->m, last);
	len += printKRIVINE(cfg + len, t->n, 'a');
    } else {
	len += printKRIVINE(cfg + len, t->m, last);
	len += sprintf(cfg + len, " (");
	len += printKRIVINE(cfg + len, t->n, '(');
	len += sprintf(cfg + len, ")");
    }
#endif
    return len;
}

int iscontent(char *cp){
	if(cp[0] == '/')
		return 1;
	return 0;
}
//------------------------------------------------------------

int choose_parameter(struct configuration_s *config){

    int num = config->fox_state->num_of_params - config->fox_state->it_routable_param;
    if(num < 0) return -1;
    while(!iscontent(config->fox_state->params[num])){
        if(--num < 0){
            return -1;
        }
    }
    return num;

}

struct ccnl_content_s *
ccnl_nfn_handle_local_computation(struct ccnl_relay_s *ccnl, struct configuration_s *config,
        char **namecomp, char *comp, int *halt){
    int complen = sprintf(comp, "call %d ", config->fox_state->num_of_params);
    struct ccnl_interest_s * interest;
    struct ccnl_content_s *c;
    int i, len;
    DEBUGMSG(99, "ccnl_nfn_handle_local_computation()\n");
    for(i = 0; i < config->fox_state->num_of_params; ++i){
        complen += sprintf(comp+complen, "%s ", config->fox_state->params[i]);
    }
    i = 0;
    namecomp[i++] = "COMPUTE";
    namecomp[i++] = strdup(comp);
    if(config->fox_state->thunk_request) namecomp[i++] = "THUNK";
    namecomp[i++] = "NFN";
    namecomp[i++] = NULL;

    interest = mkInterestObject(ccnl, config, namecomp);
    for(i = 0; i < interest->prefix->compcnt; ++i){
        printf("/%s", interest->prefix->comp[i]);
    }printf("\n");
    //TODO: Check if it is already available locally
    
    if((c = ccnl_nfn_global_content_search(ccnl, config, interest, 0)) != NULL){
        DEBUGMSG(49, "Content found in the network\n");
        return c;
    }
    //ccnl_interest_remove(ccnl, interest);
    *halt = -1;
    return 0;
}

struct ccnl_content_s *
ccnl_nfn_handle_network_search(struct ccnl_relay_s *ccnl, struct configuration_s *config, 
        int parameter_num, char **namecomp, char *comp, int *halt, int search_only_local){
    
    struct ccnl_content_s *c;
    int j;
    int complen = sprintf(comp, "(@x call %d ", config->fox_state->num_of_params);
    DEBUGMSG(2, "ccnl_nfn_handle_network_search()\n");
    
    for(j = 0; j < config->fox_state->num_of_params; ++j){
        if(parameter_num == j){
            complen += sprintf(comp + complen, "x ");
        }
        else{
            complen += sprintf(comp + complen, "%s ", config->fox_state->params[j]);
        }
    }
    complen += sprintf(comp + complen, ")");
    DEBUGMSG(49, "Computation request: %s %s\n", comp, config->fox_state->params[parameter_num]);
    //make interest
    add_computation_components(namecomp, config->fox_state->thunk_request, comp);
    struct ccnl_interest_s *interest = mkInterestObject(ccnl, config, namecomp);
    if((c = ccnl_nfn_global_content_search(ccnl, config, interest, search_only_local)) != NULL){
        DEBUGMSG(49, "Content found in the network\n");
        return c;
    }
    if(!search_only_local)*halt = -1;
    return NULL;
}

struct ccnl_content_s *
ccnl_nfn_handle_routable_content(struct ccnl_relay_s *ccnl, 
        struct configuration_s *config, int parameter_num, int *halt, int search_only_local){
    char *comp =  ccnl_malloc(sizeof(char) * CCNL_MAX_PACKET_SIZE);
    char *namecomp[CCNL_MAX_NAME_COMP];
    char *param;
    struct ccnl_content_s *c;
    int j;
    memset(comp, 0, CCNL_MAX_PACKET_SIZE);
    
    param = strdup(config->fox_state->params[parameter_num]);
    j = splitComponents(param, namecomp);
    
    if(ccnl_nfn_local_content_search(ccnl, namecomp, j)){
        DEBUGMSG(49, "Routable content %s is local availabe --> start computation\n",
                config->fox_state->params[parameter_num]);
        c = ccnl_nfn_handle_local_computation(ccnl, config, namecomp, comp, halt);
    }else{
        DEBUGMSG(49, "Routable content %s is not local availabe --> start search in the network\n",
                config->fox_state->params[parameter_num]);
        c = ccnl_nfn_handle_network_search(ccnl, config, parameter_num ,namecomp, comp, halt, search_only_local);
    }
    return c;
}

//------------------------------------------------------------
/**
 * used to avoid code duplication for popping two integer values from result stack asynchronous
 * requires to define i1 and i2 before calling to store the results.
 */
#define pop2int() \
        do{     \
            char *h1, *h2; \
            h1 = pop_or_resolve_from_result_stack(ccnl, config, restart); \
            if(h1 == NULL){ \
                *halt = -1; \
                return prog; \
            } \
            i1 = atoi(h1); \
            h2 = pop_or_resolve_from_result_stack(ccnl, config, restart); \
            if(h2 == NULL){ \
                *halt = -1; \
                push_to_stack(&config->result_stack, h1); \
                return prog; \
            } \
            i2 = atoi(h2);    \ 
        }while(0) ;
//------------------------------------------------------------
        
char*
ZAM_term(struct ccnl_relay_s *ccnl, struct configuration_s *config, 
        int thunk_request, int *num_of_required_thunks,  
        int *halt, char *dummybuf, int *restart)
{
    struct term_s *t;
    char *pending, *p, *cp;
    int len;
    char *prog = config->prog;
    memset(dummybuf, 0, 2000);
    
    //pop closure
    if (!prog || strlen(prog) == 0) {
         if(config->result_stack){
		return config->result_stack->content;
         }
         DEBUGMSG(2, "no result returned\n");
         return NULL;
    }


    pending = strchr(prog, ';');
    p = strchr(prog, '(');

    if (!strncmp(prog, "ACCESS(", 7)) {
	char *cname, *sname;
	if (pending){
	    cp = pending-1;
        }
        else{
	    cp = prog + strlen(prog) - 1;
        }
        len = cp - p;
        cp = malloc(len);
        memcpy(cp, p+1, len-1);
        cp[len-1] = '\0';
        DEBUGMSG(2, "---to do: access <%s>\n", cp);

	struct closure_s *closure = search_in_environment(config->env, cp);
	if (!closure) {
		closure = search_in_environment(config->global_dict, cp);
		if(!closure){
			DEBUGMSG(2, "?? could not lookup var %s\n", cp);
			return 0;
		}
	}
	push_to_stack(&config->argument_stack, closure);
	
	return pending+1;
    }
    if (!strncmp(prog, "CLOSURE(", 8)) {
        if (pending)
            cp = pending-1;
        else
            cp = prog + strlen(prog) - 1;
	len = cp - p;
        cp = malloc(len);
        memcpy(cp, p+1, len-1);
        cp[len-1] = '\0';
        DEBUGMSG(2, "---to do: closure <%s>\n", cp);

        if (!config->argument_stack && !strncmp(cp, "RESOLVENAME(", 12)) {
            char v[500], *c;
            int len;
            c = strchr(cp+12, ')'); if (!c) goto normal;
	    len = c - (cp+12);
	    memcpy(v, cp+12, len);
	    v[len] = '\0';
	    struct closure_s *closure = search_in_environment(config->env, v);
	    if(!closure) goto normal;
            if(!strcmp(closure->term, cp)){
                DEBUGMSG(2, "** detected tail recursion case %s\n", closure->term);
            }else{
                goto normal;
            }
        }
        else{
            struct closure_s *closure;
normal:
            closure = new_closure(cp, config->env); 
            //configuration->env = NULL;//FIXME new environment?
            push_to_stack(&config->argument_stack, closure);
        }
        if(pending){
            cp = pending + 1;
        }
        else{
	    printf("** not implemented %d\n", __LINE__);
        }
        return cp;
    }
    if (!strncmp(prog, "GRAB(", 5)) {
        char *v, *cname;
        if (pending)
	    v = pending-1;
	else
	    v = prog + strlen(prog) - 1;
	len = v - p;
	v = malloc(len);
	memcpy(v, p+1, len-1);
	v[len-1] = '\0';
	DEBUGMSG(2, "---to do: grab <%s>\n", v);

	struct closure_s *closure = pop_from_stack(&config->argument_stack);
	add_to_environment(&config->env, v, closure);

	if(pending)
            pending++;
        return pending;
    }
    if (!strncmp(prog, "TAILAPPLY", 9)) {
        char *cname, *code;
        DEBUGMSG(2, "---to do: tailapply\n");

        struct closure_s *closure = pop_from_stack(&config->argument_stack);
        code = closure->term;
        if(pending){ //build new term
            sprintf(dummybuf, "%s%s", code, pending);
        }else{
            sprintf(dummybuf, "%s", code);
        }
	config->env = closure->env; //set environment from closure
        return strdup(dummybuf);
    }
    if (!strncmp(prog, "RESOLVENAME(", 12)) {
	char res[1000];
	memset(res,0,sizeof(res));
        if (pending)
	    cp = pending-1;
	else
	    cp = prog + strlen(prog) - 1;
        len = cp - p;
        cp = malloc(len);
	memcpy(cp, p+1, len-1);
	cp[len-1] = '\0';
	DEBUGMSG(2, "---to do: resolveNAME <%s>\n", cp);

        //function definition
        if(!strncmp(cp, "let", 3)){
            DEBUGMSG(99, "Function definition: %s\n", cp);
            strcpy(res, cp+3);
            int i, end = 0, pendinglength, namelength, lambdalen;
            char *h, *name, *lambda_expr, *resolveterm;
            for(i = 0; i < strlen(res); ++i){
                if(!strncmp(res+i, "endlet", 6)){
                    end = i;
                    break;
                }
            }
            pendinglength = strlen(res+end) + strlen("RESOLVENAME()");
            h = strchr(cp, '=');
            namelength = h - cp;
            
            
            lambda_expr = malloc(strlen(h));
            name = malloc(namelength);
            pending = malloc(pendinglength);
            
            memset(pending, 0, pendinglength);
            memset(name, 0, namelength);
            memset(lambda_expr, 0, strlen(h));
            
            sprintf(pending, "RESOLVENAME(%s)", res+end+7); //add 7 to overcome endlet
            
            memcpy(name, cp+3, namelength-3); //copy name without let and endlet
            trim(name);
   
            lambdalen = strlen(h)-strlen(pending)+11-6;
            memcpy(lambda_expr, h+1, lambdalen); //copy lambda expression without =
            trim(lambda_expr);
            resolveterm = malloc(strlen("RESOLVENAME()")+strlen(lambda_expr));
            sprintf(resolveterm, "RESOLVENAME(%s)", lambda_expr);
            
            struct closure_s *cl = new_closure(resolveterm, NULL);
            add_to_environment(&config->env, name, cl);
            
            return pending;
        }
        
        //check if term can be made available, if yes enter it as a var
	//try with searching in global env for an added term!
        t = parseKRIVINE(0, &cp);
	if (term_is_var(t)) {
	    char *end = 0;
            cp = t->v;
            if (isdigit(*cp)) {
		// is disgit...
		strtol(cp, &end, 0);
		if (end && *end)
		    end = 0;
	    }
            else if(iscontent(cp)){
	   	// is content... 
		DEBUGMSG(99, "VAR IS CONTENT: %s\n", cp);
		end = cp;
	    }
            if (end) {
		push_to_stack(&config->result_stack, cp);
		if (pending)
		    sprintf(res, "TAILAPPLY%s", pending);
		else
		    sprintf(res, "TAILAPPLY");
	    }else {
		if (pending)
		    sprintf(res, "ACCESS(%s);TAILAPPLY%s", t->v, pending);
		else
		    sprintf(res, "ACCESS(%s);TAILAPPLY", t->v);
	    }
            return strdup(res);
        }
	if (term_is_lambda(t)) {
            char *var;
	    var = t->v;
	    printKRIVINE(dummybuf, t->m, 0);
	    cp = strdup(dummybuf);
            if (pending)
		sprintf(res, "GRAB(%s);RESOLVENAME(%s)%s", var, cp, pending);
	    else
		sprintf(res, "GRAB(%s);RESOLVENAME(%s)", var, cp);
	    free(cp);
            return strdup(res);
        }
        if (term_is_app(t)) {
            printKRIVINE(dummybuf, t->n, 0);
	    p = strdup(dummybuf);
	    printKRIVINE(dummybuf, t->m, 0);
	    cp = strdup(dummybuf);
            
            if (pending)
  		sprintf(res, "CLOSURE(RESOLVENAME(%s));RESOLVENAME(%s)%s", p, cp, pending);
	    else
		sprintf(res, "CLOSURE(RESOLVENAME(%s));RESOLVENAME(%s)", p, cp);
	   
            return strdup(res);
        }
    }
    if (!strncmp(prog, "OP_CMPEQ_CHURCH", 15)) {
	int i1, i2, acc;
	char *h;
        char res[1000];
	memset(res, 0, sizeof(res));
	DEBUGMSG(2, "---to do: OP_CMPEQ <%s>/<%s>\n", cp, pending);
	pop2int();
	acc = i1 == i2;
        cp =  acc ? "@x@y x" : "@x@y y";
        if (pending)
	    sprintf(res, "RESOLVENAME(%s)%s", cp, pending);
	else
	    sprintf(res, "RESOLVENAME(%s)", cp);
	return strdup(res);
    }
    if (!strncmp(prog, "OP_CMPLEQ_CHURCH", 16)) {
	int i1, i2, acc;
	char *h;
        char res[1000];
	memset(res, 0, sizeof(res));
	DEBUGMSG(2, "---to do: OP_CMPLEQ <%s>/%s\n", cp, pending);
	pop2int();
	acc = i2 <= i1;
        cp =  acc ? "@x@y x" : "@x@y y";
        if (pending)
	    sprintf(res, "RESOLVENAME(%s)%s", cp, pending);
	else
	    sprintf(res, "RESOLVENAME(%s)", cp);
	return strdup(res);
    }
    if (!strncmp(prog, "OP_CMPEQ", 8)) {
	int i1, i2, acc;
	char *h;
        char res[1000];
	memset(res, 0, sizeof(res));
	DEBUGMSG(2, "---to do: OP_CMPEQ <%s>/<%s>\n", cp, pending);
	pop2int();
	acc = i1 == i2;
        if(acc)
            cp = "1";
        else
            cp = "0";
        if (pending)
	    sprintf(res, "RESOLVENAME(%s)%s", cp, pending);
	else
	    sprintf(res, "RESOLVENAME(%s)", cp);
	return strdup(res);
    }
    if (!strncmp(prog, "OP_CMPLEQ", 9)) {
	int i1, i2, acc;
	char *h;
        char res[1000];
	memset(res, 0, sizeof(res));
	DEBUGMSG(2, "---to do: OP_CMPLEQ <%s>/%s\n", cp, pending);
	pop2int();
	acc = i2 <= i1;
        if(acc)
            cp = "1";
        else
            cp = "0";
        if (pending)
	    sprintf(res, "RESOLVENAME(%s)%s", cp, pending);
	else
	    sprintf(res, "RESOLVENAME(%s)", cp);
	return strdup(res);
    }
    if (!strncmp(prog, "OP_ADD", 6)) {
	int i1, i2, res;
	char *h;
	DEBUGMSG(2, "---to do: OP_ADD <%s>\n", prog+7);
	pop2int();
	res = i1+i2;
	h = malloc(sizeof(char)*10);
        memset(h,0,10);
	sprintf(h, "%d", res);
	push_to_stack(&config->result_stack, h);
	return pending+1;
    }
    if (!strncmp(prog, "OP_SUB", 6)) {
	int i1, i2, res;
	char *h;
	DEBUGMSG(2, "---to do: OP_SUB <%s>\n", prog+7);
        pop2int();
	res = i2-i1;
	h = malloc(sizeof(char)*10);
        memset(h,0,10);
	sprintf(h, "%d", res);
	push_to_stack(&config->result_stack, h);
	return pending+1;
    }
    if (!strncmp(prog, "OP_MULT", 7)) {
	int i1, i2, res;
	char *h;
	DEBUGMSG(2, "---to do: OP_MULT <%s>\n", prog+8);
        pop2int();
	res = i1*i2;
	h = malloc(sizeof(char)*10);
        memset(h,0,10);
	sprintf(h, "%d", res);
	push_to_stack(&config->result_stack, h);
	return pending+1;
    }
    if(!strncmp(prog, "OP_IFELSE", 9)){
        char *h;
        int i1;
        h = pop_or_resolve_from_result_stack(ccnl, config, restart);
        DEBUGMSG(2, "---to do: OP_IFELSE <%s>\n", prog+10);
        if(h == NULL){
            *halt = -1;
            return prog;
        }
        i1 = atoi(h);
        if(i1){
            DEBUGMSG(99, "Execute if\n");
            struct closure_s *cl = pop_from_stack(&config->argument_stack);
            pop_from_stack(&config->argument_stack);
            push_to_stack(&config->argument_stack, cl);
        }
        else{
            DEBUGMSG(99, "Execute else\n");
            pop_from_stack(&config->argument_stack);
        }
        return pending+1;
    }
    if(!strncmp(prog, "OP_CALL", 7)){
	char *h;
	int i, offset;
	char name[5];
        DEBUGMSG(2, "---to do: OP_CALL <%s>\n", prog+7);
        h = pop_or_resolve_from_result_stack(ccnl, config, restart);
	int num_params = atoi(h);
        memset(dummybuf, 0, sizeof(dummybuf));
	sprintf(dummybuf, "CLOSURE(OP_FOX);RESOLVENAME(@op(");///x(/y y x 2 op)));TAILAPPLY";
        offset = strlen(dummybuf);	
	for(i = 0; i < num_params; ++i){
	    sprintf(name, "x%d", i);
	    offset += sprintf(dummybuf+offset, "@%s(", name);
	}
	for(i =  num_params - 1; i >= 0; --i){
	    sprintf(name, "x%d", i);
	    offset += sprintf(dummybuf+offset, " %s", name);
	}
        offset += sprintf(dummybuf + offset, " %d", num_params);
	offset += sprintf(dummybuf+offset, " op");
	for(i = 0; i < num_params+2; ++i){
            offset += sprintf(dummybuf + offset, ")");
	}
        offset += sprintf(dummybuf + offset, "%s", pending);
	return strdup(dummybuf);
    }
    if(!strncmp(prog, "OP_FOX", 6)){
        int search_only_local = 0;
        if(*restart) {
            *restart = 0;
            search_only_local = 1;
            goto recontinue;
        }
        DEBUGMSG(2, "---to do: OP_FOX <%s>\n", prog+7);
        char *h = pop_or_resolve_from_result_stack(ccnl, config, restart);
        config->fox_state->num_of_params = atoi(h);
        DEBUGMSG(99, "NUM OF PARAMS: %d\n", config->fox_state->num_of_params);
        int i;
        config->fox_state->params = malloc(sizeof(char * ) * config->fox_state->num_of_params); 
        
        for(i = 0; i < config->fox_state->num_of_params; ++i){ //pop parameter from stack
            config->fox_state->params[i] = pop_or_resolve_from_result_stack(ccnl, config, restart);
        }
        //as long as there is a routable parameter: try to find a result
        config->fox_state->it_routable_param = 0;
        int parameter_number = -1;
        struct ccnl_content_s *c = NULL;
recontinue:



        //check if last result is now available
        if(search_only_local){
            //choose  routable parameter
            parameter_number = choose_parameter(config);

            c = ccnl_nfn_handle_routable_content(ccnl,
                                    config, parameter_number, halt, search_only_local);
            search_only_local = 0;
        }

        //result was not locally found
        if(!c){
            //choose next parameter
            ++config->fox_state->it_routable_param;
            parameter_number = choose_parameter(config);

            //no more parameter --> no result found
            if(parameter_number < 0) return NULL;

            c = ccnl_nfn_handle_routable_content(ccnl,
                                    config, parameter_number, halt, search_only_local);

            //wait for content, return current program to continue later
            if(*halt < 0) return prog;
        }

        if(c){
            if(thunk_request){ //if thunk_request push thunkid on the stack

                --(*num_of_required_thunks);
                char *thunkid = ccnl_nfn_add_thunk(ccnl, config, c->name);
                char *time_required = c->content;
                int thunk_time = strtol(time_required, NULL, 10);
                thunk_time = thunk_time > 0 ? thunk_time : NFN_DEFAULT_WAITING_TIME;
                config->thunk_time = config->thunk_time > thunk_time ? config->thunk_time : thunk_time;
                DEBUGMSG(99, "Got thunk %s, now %d thunks are required\n", thunkid, *num_of_required_thunks);
                push_to_stack(&config->result_stack, thunkid);
                if( *num_of_required_thunks <= 0){
                    DEBUGMSG(99, "All thunks are available\n");
                    ccnl_nfn_reply_thunk(ccnl, config);
                }
            }
            else{
                push_to_stack(&config->result_stack, c->content);
            }
        }
/*
        for(; config->fox_state->it_routable_param >= 0; --config->fox_state->it_routable_param){
            if(iscontent(config->fox_state->params[config->fox_state->it_routable_param])){
                DEBUGMSG(99, "Using %s as routable content\n", config->fox_state->params[config->fox_state->it_routable_param]);
                struct ccnl_content_s *c = ccnl_nfn_handle_routable_content(ccnl,
                        config, halt, search_only_local);
                search_only_local = 0;
                if(*halt < 0) return prog;
                if(c){
                    if(thunk_request){ //if thunk_request push thunkid on the stack
                        
                        --(*num_of_required_thunks);
                        char *thunkid = ccnl_nfn_add_thunk(ccnl, config, c->name);
                        char *time_required = c->content;
                        int thunk_time = strtol(time_required, NULL, 10);
                        thunk_time = thunk_time > 0 ? thunk_time : NFN_DEFAULT_WAITING_TIME;
                        config->thunk_time = config->thunk_time > thunk_time ? config->thunk_time : thunk_time;
                        DEBUGMSG(99, "Got thunk %s, now %d thunks are required\n", thunkid, *num_of_required_thunks);
                        push_to_stack(&config->result_stack, thunkid);
                        if( *num_of_required_thunks <= 0){
                            DEBUGMSG(99, "All thunks are available\n");
                            ccnl_nfn_reply_thunk(ccnl, config);
                        }
                    }
                    else{
                        push_to_stack(&config->result_stack, c->content);
                    }                    
                    goto tail;
                }
            }//endif
        }//endfor
*/
        
        DEBUGMSG(99, "Pending: %s\n", pending+1);
        return pending+1;
    }
    
    if(!strncmp(prog, "halt", 4)){
	*halt = 1;
        return pending;
        //FIXME: create string from stack, allow particular reduced
        //return config->result_stack->content;
    }

    DEBUGMSG(2, "unknown built-in command <%s>\n", prog);

    return 0;
}


//----------------------------------------------------------------
void
setup_global_environment(struct environment_s **env){

    //Operator on Church numbers
    struct closure_s *closure = new_closure("RESOLVENAME(@x@y x)", NULL);
    add_to_environment(env, "true_church", closure);

    closure = new_closure("RESOLVENAME(@x@y y)", NULL);
    add_to_environment(env, "false_church", closure);

    closure = new_closure("CLOSURE(OP_CMPEQ_CHURCH);RESOLVENAME(@op(@x(@y x y op)))", NULL);
    add_to_environment(env, "eq_church", closure);

    closure = new_closure("CLOSURE(OP_CMPLEQ_CHURCH);RESOLVENAME(@op(@x(@y x y op)))", NULL);
    add_to_environment(env, "leq_church", closure);

    closure = new_closure("RESOLVENAME(@expr@yes@no(expr yes no))", NULL);
    add_to_environment(env, "ifelse_church", closure);
    
    //Operator on integer numbers
    closure = new_closure("CLOSURE(OP_CMPEQ);RESOLVENAME(@op(@x(@y x y op)))", NULL);
    add_to_environment(env, "eq", closure);

    closure = new_closure("CLOSURE(OP_CMPLEQ);RESOLVENAME(@op(@x(@y x y op)))", NULL);
    add_to_environment(env, "leq", closure);
    
    closure = new_closure("CLOSURE(OP_IFELSE);RESOLVENAME(@op(@x x op));TAILAPPLY", NULL);
    add_to_environment(env, "ifelse", closure);

    closure = new_closure("CLOSURE(OP_ADD);RESOLVENAME(@op(@x(@y x y op)));TAILAPPLY", NULL);
    add_to_environment(env, "add", closure);

    closure = new_closure("CLOSURE(OP_SUB);RESOLVENAME(@op(@x(@y x y op)));TAILAPPLY", NULL);
    add_to_environment(env, "sub", closure);
   
    closure = new_closure("CLOSURE(OP_MULT);RESOLVENAME(@op(@x(@y x y op)));TAILAPPLY", NULL);
    add_to_environment(env, "mult", closure);

    closure = new_closure("CLOSURE(OP_CALL);RESOLVENAME(@op(@x x op));TAILAPPLY", NULL);
    add_to_environment(env, "call", closure);
}

char *
Krivine_reduction(struct ccnl_relay_s *ccnl, char *expression, int thunk_request, 
        int num_of_required_thunks, struct configuration_s **config,
        struct ccnl_prefix_s *prefix){
    DEBUGMSG(99, "Krivine_reduction()\n");
    int steps = 0; 
    int halt = 0;
    int restart = 1;
    int len = strlen("CLOSURE(halt);RESOLVENAME()") + strlen(expression);
    char *prog;
    struct environment_s *global_dict = NULL;
    char *dummybuf = malloc(2000);
    setup_global_environment(&global_dict);
    if(!*config && strlen(expression) == 0) return 0;
    prog = malloc(len*sizeof(char));
    sprintf(prog, "CLOSURE(halt);RESOLVENAME(%s)", expression);
    if(!*config){
        *config = new_config(prog, global_dict, thunk_request,
                num_of_required_thunks, prefix, configid);
        DBL_LINKED_LIST_ADD(configuration_list, *config);
        restart = 0;
        --configid;
    }
    DEBUGMSG(99, "Prog: %s\n", (*config)->prog);

    while ((*config)->prog && !halt && (*config)->prog != 1){
	steps++;
	DEBUGMSG(1, "Step %d: %s\n", steps, (*config)->prog);
	(*config)->prog = ZAM_term(ccnl, (*config), (*config)->fox_state->thunk_request, 
                &(*config)->fox_state->num_of_required_thunks, &halt, dummybuf, &restart);
    }
    if(halt < 0){
        //put config in stack
        //configuration_list[-(*config)->configid] = (*config);
        DEBUGMSG(99,"Pause computation: %d\n", -(*config)->configid);
        return NULL;
    }
    else{
        free(dummybuf);
        DEBUGMSG(99, "end\n");
        char *h = pop_or_resolve_from_result_stack(ccnl, *config, &restart);//config->result_stack->content;
        if(h == NULL){
            halt = -1;
            return NULL;
        }
        return h;
    }
}
