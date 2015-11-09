/* gcc gc.c -O2 -Wall -Wextra -lm -o gc && ./gc */
#define INITIAL_ARRAY_SIZE 16
#define STACK_SIZE 256
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * This code shows how to write a simple "stop-the-world mark and sweep" garbage
 * collector. It is based on the excellent explanation "Baby's First Garbage
 * Collector" by Bob Nystrom. Check it out:
 *
 * http://journal.stuffwithstuff.com/2013/12/08/babys-first-garbage-collector/
 *
 * First we implement our own type system. We will restrict ourselves to four
 * data types: dynamic arrays, numbers, pairs and strings. A dynamic array is an
 * array that can grow and shrink. A pair is a simple data structure, basically
 * an array of two elements, which is used mainly in lisp-like languages to
 * construct more complex data structures. The elements of a pair are called car
 * and cdr in lisp, but here they are called head and tail. Of course you can
 * expand the type system to contain more data types.
 *
 * An object in our system is a "tagged union" with a type tag telling us which
 * data type it is. Objects also contain a mark used by the garbage collector
 * (1 = reachable; 0 = unreachable) and a pointer to another object so that we
 * can implement a linked list of objects.
 */
enum Type
{
    ARRAY,
    NUMBER,
    PAIR,
    STRING
};

struct Object
{
    enum Type type;
    int mark;
    struct Object *next;
    union
    {
        struct
        {
            int length;
            int size;
            struct Object **array;
        };
        double number;
        struct
        {
            struct Object *head;
            struct Object *tail;
        };
        char *string;
    };
};

/*
 * We implement a linked list of all the objects currently in existence,
 * reachable or unreachable.
 */
struct Object *list_of_objects = NULL;

/*
 * This function creates a new object and adds it to the linked list. Don't use
 * this function directly, call the type specific functions instead.
 */
struct Object *new_object(void)
{
    struct Object *object = malloc(sizeof(struct Object));
    object->mark = 0;
    object->next = list_of_objects;
    list_of_objects = object;
    return object;
}

/*
 * Each array object has an array of pointers to objects, but this array is
 * allocated somewhere else and not directly part of the tagged union, which
 * contains only a pointer to the array. If the array where part of the tagged
 * union, we would have to have a fixed array size and the array would
 * unnecessarily increase the size of the tagged union.
 */
struct Object *new_array(void)
{
    struct Object *object = new_object();
    object->type = ARRAY;
    object->length = 0;
    object->size = INITIAL_ARRAY_SIZE;
    object->array = calloc(INITIAL_ARRAY_SIZE, sizeof(struct Object *));
    return object;
}

struct Object *new_number(double number)
{
    struct Object *object = new_object();
    object->type = NUMBER;
    object->number = number;
    return object;
}

struct Object *new_pair(struct Object *head, struct Object *tail)
{
    struct Object *object = new_object();
    object->type = PAIR;
    object->head = head;
    object->tail = tail;
    return object;
}

struct Object *new_string(char *string)
{
    struct Object *object = new_object();
    object->type = STRING;
    object->string = calloc(strlen(string) + 1, sizeof(char));
    strcpy(object->string, string);
    return object;
}

/*
 * A function to delete objects. Never call this function directly, as it will
 * not properly unchain the object from the linked list. Call the garbage
 * collector instead and let it do all the clean-up.
 *
 * Note that elements of data structures are not deleted recursively. The might
 * still be referenced from somewhere else and we'll leave it to the garbage
 * collector to figure that out.
 */
void delete_object(struct Object *object)
{
    if (object)
    {
        switch (object->type)
        {
            case ARRAY:
                free(object->array);
                break;
            case NUMBER:
                break;
            case PAIR:
                break;
            case STRING:
                free(object->string);
                break;
        }
        free(object);
    }
}

/*
 * Functions for array operations.
 */
void append_element(struct Object *array, struct Object *element)
{
    if (array->length == array->size)
    {
        array->size *= 2;
        array->array = realloc(array->array, array->size * sizeof(struct Object *));
    }
    array->array[array->length] = element;
    array->length++;
}

struct Object *get_element(struct Object *array, int index)
{
    return array->array[index];
}

void set_element(struct Object *array, int index, struct Object *element)
{
    array->array[index] = element;
}

/*
 * Functions that print objects in a human readable form. Pairs are printed in a
 * lisp-like fashion.
 */
void print_array(struct Object *);
void print_object(struct Object *);

void print_array(struct Object *array)
{
    int i;
    putchar('[');
    for (i = 0; i < array->length; i++)
    {
        if (i > 0)
        {
            fputs(", ", stdout);
        }
        print_object(array->array[i]);
    }
    putchar(']');
}

void print_object(struct Object *object)
{
    if (object)
    {
        switch (object->type)
        {
            case ARRAY:
                print_array(object);
                break;
            case NUMBER:
                printf("%g", object->number);
                break;
            case PAIR:
                putchar('(');
                print_object(object->head);
                object = object->tail;
                while (object && object->type == PAIR)
                {
                    putchar(' ');
                    print_object(object->head);
                    object = object->tail;
                }
                if (object)
                {
                    fputs(" . ", stdout);
                    print_object(object->tail);
                }
                putchar(')');
                break;
            case STRING:
                putchar('"');
                fputs(object->string, stdout);
                putchar('"');
                break;
        }
    }
    else
    {
        fputs("null", stdout);
    }
}

/*
 * We implement a little stack that could be part of a virtual machine.
 */
struct Object *stack[STACK_SIZE];
int stack_length = 0;

void push(struct Object *object)
{
    stack[stack_length] = object;
    stack_length++;
}

struct Object *pop(void)
{
    stack_length--;
    return stack[stack_length];
}

struct Object *peek(void)
{
    return stack[stack_length - 1];
}

/*
 * Our garbage collector will have to mark reachable objects. However if a
 * reachable object is a data structure, then its elements must also be marked.
 * If they are also data structures, then their elements must also be marked and
 * so on. Thus we need a recursive function.
 *
 * Cyclical references could lead to an infinite recursion. To avoid this, we
 * won't mark objects already marked.
 */
void mark_elements(struct Object *);
void mark_object(struct Object *);

void mark_elements(struct Object *array)
{
    int i;
    for (i = 0; i < array->length; i++)
    {
        mark_object(array->array[i]);
    }
}

void mark_object(struct Object *object)
{
    if (object && !object->mark)
    {
        object->mark = 1;
        switch (object->type)
        {
            case ARRAY:
                mark_elements(object);
                break;
            case NUMBER:
                break;
            case PAIR:
                mark_object(object->head);
                mark_object(object->tail);
                break;
            case STRING:
                break;
        }
    }
}

/*
 * A function that marks all objects on the stack or reachable from the stack.
 */
void mark(void)
{
    int i;
    for (i = 0; i < stack_length; i++)
    {
        mark_object(stack[i]);
    }
}

/*
 * After calling mark() all reachable objects are marked. Now we need a function
 * to go through the linked list, unchain unmarked objects and free them. If we
 * encounter a marked object, we will just remove the mark for the next GC
 * cycle.
 *
 * Nystrom uses a cool trick with a pointer to a pointer here, which is awesome
 * but also difficult to understand. I go for a more readable approach with an
 * extra variable "previous".
 */
void sweep(void)
{
    struct Object *object = list_of_objects;
    struct Object *previous = NULL;
    struct Object *garbage;
    while (object)
    {
        if (object->mark)
        {
            fputs("I won't delete this: ", stdout);
            print_object(object);
            putchar('\n');
            object->mark = 0;
            previous = object;
            object = object->next;
        }
        else
        {
            fputs("I will delete this: ", stdout);
            print_object(object);
            putchar('\n');
            if (previous)
            {
                previous->next = object->next;
            }
            else
            {
                list_of_objects = object->next;
            }
            garbage = object;
            object = object->next;
            delete_object(garbage);
        }
    }
}

void stop_the_world_mark_and_sweep(void)
{
    mark();
    sweep();
}

/*
 * Now let's implement a simple stack-oriented language with the following
 * features:
 * - literal numbers are pushed onto the stack.
 * - "add", "sub", "mul", "div" and "mod" pop two operands from the stack,
 *   perform an arithmetic operation and push the result onto the stack.
 * - "pop" pops a value from the stack.
 * - "print" prints the top of the stack without popping it.
 * - "null" pushes a null pointer onto the stack.
 * - "cons" pops two values from the stack, constructs a pair and pushes it onto
 *   the stack.
 *
 * This language lacks support for strings and arrays even though our type
 * system includes them, but you can always expand the language if you like.
 */
enum TokenType
{
    ADD_TOKEN,
    CONS_TOKEN,
    DIV_TOKEN,
    END_TOKEN,
    MOD_TOKEN,
    MUL_TOKEN,
    NULL_TOKEN,
    NUMBER_TOKEN,
    POP_TOKEN,
    PRINT_TOKEN,
    SUB_TOKEN
};

struct Token
{
    enum TokenType type;
    struct Object *value;
};

char code[] = "1 2 add 3 mul print pop 1 2 3 null cons cons cons print";
char *to = code;
char *from;

/*
 * This implementation lacks checks to handle syntax and runtime errors because
 * it is only a demonstration. Of course a real language should have such
 * checks.
 */
struct Token next_token(void)
{
    struct Token token;
    char substring[256];
    from = to;
    if (*to == '\0')
    {
        token.type = END_TOKEN;
    }
    else if ((*to >= '\t' && *to <= '\r') || *to == ' ')
    {
        to++;
        while ((*to >= '\t' && *to <= '\r') || *to == ' ')
        {
            to++;
        }
        return next_token();
    }
    else if (*to == '+' || *to == '-' || (*to >= '0' && *to <= '9'))
    {
        to++;
        while (*to >= '0' && *to <= '9')
        {
            to++;
        }
        if (*to == '.')
        {
            to++;
            while (*to >= '0' && *to <= '9')
            {
                to++;
            }
        }
        if (*to == 'E' || *to == 'e')
        {
            to++;
            if (*to == '+' || *to == '-')
            {
                to++;
            }
            while (*to >= '0' && *to <= '9')
            {
                to++;
            }
        }
        strncpy(substring, from, to - from);
        substring[to - from] = '\0';
        token.type = NUMBER_TOKEN;
        token.value = new_number(atof(substring));
    }
    else if (*to >= 'a' && *to <= 'z')
    {
        to++;
        while (*to >= 'a' && *to <= 'z')
        {
            to++;
        }
        strncpy(substring, from, to - from);
        substring[to - from] = '\0';
        if (strcmp(substring, "add") == 0)
        {
            token.type = ADD_TOKEN;
        }
        else if (strcmp(substring, "cons") == 0)
        {
            token.type = CONS_TOKEN;
        }
        else if (strcmp(substring, "div") == 0)
        {
            token.type = DIV_TOKEN;
        }
        else if (strcmp(substring, "mod") == 0)
        {
            token.type = MOD_TOKEN;
        }
        else if (strcmp(substring, "mul") == 0)
        {
            token.type = MUL_TOKEN;
        }
        else if (strcmp(substring, "null") == 0)
        {
            token.type = NULL_TOKEN;
        }
        else if (strcmp(substring, "pop") == 0)
        {
            token.type = POP_TOKEN;
        }
        else if (strcmp(substring, "print") == 0)
        {
            token.type = PRINT_TOKEN;
        }
        else if (strcmp(substring, "sub") == 0)
        {
            token.type = SUB_TOKEN;
        }
    }
    return token;
}

void interpret(void)
{
    struct Token token;
    struct Object *operand1;
    struct Object *operand2;
    while (1)
    {
        token = next_token();
        switch (token.type)
        {
            case ADD_TOKEN:
                operand2 = pop();
                operand1 = pop();
                push(new_number(operand1->number + operand2->number));
                break;
            case CONS_TOKEN:
                operand2 = pop();
                operand1 = pop();
                push(new_pair(operand1, operand2));
                break;
            case DIV_TOKEN:
                operand2 = pop();
                operand1 = pop();
                push(new_number(operand1->number / operand2->number));
                break;
            case END_TOKEN:
                return;
            case MOD_TOKEN:
                operand2 = pop();
                operand1 = pop();
                push(new_number(fmod(operand1->number, operand2->number)));
                break;
            case MUL_TOKEN:
                operand2 = pop();
                operand1 = pop();
                push(new_number(operand1->number * operand2->number));
                break;
            case NULL_TOKEN:
                push(NULL);
                break;
            case NUMBER_TOKEN:
                push(token.value);
                break;
            case POP_TOKEN:
                pop();
                break;
            case PRINT_TOKEN:
                print_object(peek());
                putchar('\n');
                break;
            case SUB_TOKEN:
                operand2 = pop();
                operand1 = pop();
                push(new_number(operand1->number - operand2->number));
                break;
        }
    }
}

/*
 * And we are done. Let's start the interpreter and then our garbage collector.
 */
int main(void)
{
    interpret();
    putchar('\n');
    stop_the_world_mark_and_sweep();
    return 0;
}
