#define INITIAL_ARRAY_SIZE 16
#define STACK_SIZE 256
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
 * Functions that print objects in a human readable form.
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
 * In the main function we will push and pop some objects on the stack. This is
 * essentially what a virtual machine does during runtime. After a couple of
 * stack operations we call the garbage collector.
 */
int main(void)
{
    int i;
    struct Object *array = new_array();;
    push(new_pair(new_number(1), new_pair(new_number(2), new_pair(new_number(3), NULL))));
    push(new_number(4));
    push(new_pair(new_number(5), new_pair(new_number(6), new_pair(new_number(7), NULL))));
    pop();
    pop();
    push(new_number(8));
    push(new_string("hello"));
    push(new_string("world"));
    pop();
    for (i = 10; i <= 30; i += 10)
    {
        append_element(array, new_number(i));
    }
    set_element(array, 1, NULL);
    push(array);
    push(NULL);
    stop_the_world_mark_and_sweep();
    return 0;
}
