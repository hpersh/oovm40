# Part I - Library For Object-Oriented VM

## VM model

### Memory

### Instances

### Objects

### Threads

#### Instance stack

#### Frame stack

### Operating with C


## Built-in classes

### Metaclass

### Object

### Boolean

### Integer

## API

# Part II - Compiler for Object-oriented Language

## Language

## Goals
- Minimal
  * Keep language simple
- Consistent
  * Everything is an object

## Influences
- Syntax: C, simplified a bit, with additions
  * Assignments are statements, not expressions
  * "if", "while", "for", etc. require block with curly braces
  * "return" requires expression in parentheses
  * Additional keywords begin with '@', e,g. "@class", "@method"

## Sample
hello.ovm
```
// Yes, of course there are comments

@class Start
{
    @classmethod start(cls)
    {
        "Hello, world!\n".print();
    }
}
```
## Components
- oovm.[ch]: source for VM
- .libs/liboovm.so: VM runtime
- .libs/liboovm*module*.so: compiled module _module_
- ovmc1, ovmc[2-5].py: Source code compiler
- oovm: Executable to load initial module, and run initial method
- *.c, *.ovm: Module source

## How to build basic system
1. make all

## How to build and run a new module
1. Create module source file, eg. _module_.ovm
2. Build module, e.g. ovmc  _module_.ovm; resulting shared lib is in .libs subdirectory
3. Run module, e.g. oovm _module_[._class_[._method_]] [_arg_ ...]
   - _class_ and _method_ default to "Start" and "start", respectively

Environment variable OVM_MODULE_PATH must include location(s) for libraries for required modules, liboovm*module*.so

## Exceptions

### System exceptions

#### system.invalid-value
A method was given an invalid value, usually of the wrong type.
The attribute _inst_ contains the offending instance.

#### system.no-method
A call was made for a method that is not defined.  The following attributes
describe the offending method call:  
- _receiver_ contains the method receiver
- _selector_ contains the method selector

#### system.no-variable
An attempt was made to read a non-existent variable from a namespace, or the environment.
The attribute _name_ contains the name of the offending variable.

#### system.number-of-arguments
A method was called with the incorrect number of arguments.  The following
attributes describe the offending metho call:
- _expected_ contains the number arguments the method expected, when the method
   expects a fixed number of arguments
- _minimum_ contains the minimum number arguments the method expected, when the method
   expects a variable number of arguments
- _maximum_ contains the minimum number arguments the method expected, when the method
   expects a variable number of arguments
- _got_ contains the number of arguments the method was called with

#### system.no-attribute
An attribute read for an object failed, because the given object does not have the
given attribute.  The following attributes of the exception describe the offending access:
- _instance_ is the object for which the access was made
- _attribute_ is the requested attribute for the object

#### system.index-range
An attempt was made to access an array or slice outside its bounds.  The following attributes of the exception describe the offending access:
- _instance_ is the array or slice being accessed
- _index_ is the starting index
- _length_ is the number of items; only provided if it is > 1

#### system.key-not-found
An attempt was made to look up a key in a dictionary, and the key is not present in the dictionary.  The following attributes of the exception describe the offending access:
- _instance_ is the dictionary being accessed
- _key_ is the offending key

#### system.modify-constant
An attempt was made to modify a constant entry (a entry where key begins with '#' character) in a dictionary.    The following attributes of the exception describe the offending access:
- _instance_ is the dictionary being accessed
- _key_ is the offending key

#### system.file-open
Opening a file failed.  The following attributes of the exception describe the offending access:
- _filename_ is the name of the file
- _mode_ is the file access mode
- _errno_ is the error code returned by fopen(3)
- _message_ is a strerror_r(3)-style error message for the failure

#### system.module-load
Loading a module failed.
- _name_ is the name of the module
- _message_ is a message describing the failure

## Classes

- All classes are instances of the metaclas #Metaclass
- The parent relationship between classes is used for method search order
  - When searching for method M for an instance of class C,
    - Look in the method directory for class C
    - If not found, look in method directory of parent of C
    - Repeat moving up parents until found, stopping at #Object class, which
      has no parent
  - Similar process for instances that are classes, i.e. instances of
    #Metaclass; search uses classmethod directry for each searched class

### #Metaclass

#### Class methodies

new(ns, name, parent)  
Create a new class, which will have the given name, be entered into the
given namespace, and have the given class as its parent.

at(self, name)  
Return the <key, value> pair for the given class variable, or #nil if the
class variable is not defined.

ate(self, name)
Return the value for the given class variable; raise an exception of type



#### Instance methodies

### #Object

@class #Object
{
    // Allocate storage for instance
    @classmethod __alloc__();

    // Initialize an instance
    @method __init__(arg, ...);

    // Create a new instance
    // The class' __alloc__() method is called, to allocate storage for the
    // instance, and then the __init__() method is called for the new instance,
    // passing the arguments given to new().
    @classmethod new(arg, ...);

    // Return #false if receiver is #nil, else return #true
    @method Boolean();

    // Return a list of pairs, of all members of the object
    @method List();

    
}

### Boolean



## Bugs
- Nested functions - FIXED

## To do
- Array args - DONE
- Namepsace support - DONE
- Optimization -O2 causes crash (!) - DONE
- Parsing strings into objects - DONE
- Global (module-level) functions - already supported
  - Assignment to module variable, of anonymous function

- Complete test suite
- Performace
  * Inline everything except method calls
- Minor code-generation optimizations
  * Jumps to jumps
  * Straight-line, non-iterated code need not pre-allocate locals
- VM instructions
- More string formatting features: field width, justification, etc.
- Syntactic sugar for setting global (i.e. module-level) variables
- Static builds
- Use autoconfig / automake