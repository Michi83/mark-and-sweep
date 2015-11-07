#include <stdio.h>
#include <stdlib.h>

/*
 * This code shows how to write a simple "stop-the-world mark and sweep" garbage
 * collector. It is based on the excellent explanation "Baby's First Garbage
 * Collector" by Bob Nystrom. Check it out:
 *
 * http://journal.stuffwithstuff.com/2013/12/08/babys-first-garbage-collector/
 *
 * First we implement our own type system. We will restrict ourselves to two
 * data types: numbers and pairs. A pair is a simple data structure, basically
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
    NUMBER,
    PAIR
};

struct Object
{
    enum Type type;
    int mark;
    struct Object *next;
    union
    {
        double number_value;
        struct
        {
            struct Object *head;
            struct Object *tail;
        };
    };
};

/*
 * We implement a linked list of all the objects currently in existence,
 * reachable or unreachable.
 */
struct Object *list_of_objects = NULL;

/*
 * This function creates a new object and adds it to the linked list. Don't use
 * this function directly, call new_number() or new_pair() instead.
 */
struct Object *new_object(void)
{
    struct Object *object = (struct Object *)malloc(sizeof(struct Object));
    object->mark = 0;
    object->next = list_of_objects;
    list_of_objects = object;
    return object;
}

struct Object *new_number(double number_value)
{
    struct Object *number = new_object();
    number->type = NUMBER;
    number->number_value = number_value;
    return number;
}

struct Object *new_pair(struct Object *head, struct Object *tail)
{
    struct Object *pair = new_object();
    pair->type = PAIR;
    pair->head = head;
    pair->tail = tail;
    return pair;
}

/*
 * A function that prints objects in a human readable form.
 */
void print_object(struct Object *object)
{
    if (object)
    {
        switch (object->type)
        {
            case NUMBER:
                printf("%g", object->number_value);
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
struct Object *stack[256];
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
 * reachable object is a pair, then its head and tail must also be marked. If
 * they are also pairs, then their heads and tails must also be marked and so
 * on. Thus we need a recursive function.
 *
 * Cyclical references could lead to an infinite recursion. To avoid this, we
 * won't mark objects already marked.
 */
void mark_object(struct Object *object)
{
    if (object && !object->mark)
    {
        object->mark = 1;
        if (object->type == PAIR)
        {
            mark_object(object->head);
            mark_object(object->tail);
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
            free(garbage);
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
    push(new_pair(new_number(1), new_pair(new_number(2), new_pair(new_number(3), NULL))));
    push(new_number(4));
    push(new_pair(new_number(5), new_pair(new_number(6), new_pair(new_number(7), NULL))));
    pop();
    pop();
    push(new_number(8));
    stop_the_world_mark_and_sweep();
    return 0;
}
