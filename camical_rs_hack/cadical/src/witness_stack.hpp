#ifndef _witness_stack_hpp_INCLUDED
#define _witness_stack_hpp_INCLUDED
#include <memory_resource>
#include <cstring>
namespace CaDiCaL {

// Dynamic array for weakened clauses with a given witness. 
// The stack just consists of a int*. Size and capacity are stored in the 
// first two places of the array. 
//
struct WitnessStack {
	int *stack = nullptr;

	unsigned size () const {
		return stack ? static_cast<unsigned> (stack[0]) : 0;
	}

	unsigned capacity () const {
		return stack ? static_cast<unsigned> (stack[1]) : 0;
	}

	bool empty () const {
		return size () == 0;
	}

	void allocate (unsigned cap, std::pmr::memory_resource &resource) {
		assert (!stack);
		assert (cap >= 2);

		stack = static_cast<int *> (resource.allocate (cap * sizeof (int), alignof (int)));

		stack[0] = 2;
		stack[1] = cap;
	}

	void push_back (int val, std::pmr::memory_resource &resource) {
		if (!stack) { // empty stack <-> nullptr
			allocate (8, resource); // common case
		} else if (stack[0] == stack[1]) { // stack is full. Grow...
			grow (resource);
		}
		const unsigned old_size = size (); // old size (also idx to end)
		stack[old_size] = val; // store value at end of stack
		stack[0] = old_size + 1; // increment size
	}

	void grow (std::pmr::memory_resource &resource) {
		assert (stack);
		assert (stack[0] == stack[1]);

		const unsigned old_size = size ();
		const unsigned old_cap  = capacity ();
		unsigned new_cap;
		switch (old_cap) {
		case 8: new_cap = 12; break;
		case 12: new_cap = 16; break;
		default: new_cap = 2 * old_cap; break;
		}
		int *new_stack = static_cast<int *> (resource.allocate (new_cap * sizeof (int), alignof (int)));

		std::memcpy (new_stack, stack, old_size * sizeof (int));
		new_stack[1] = new_cap;

		resource.deallocate (stack, old_cap * sizeof (int), alignof (int));

		stack = new_stack;
	}

	void truncate (unsigned new_size, std::pmr::memory_resource &resource) {
		assert (stack);
		assert (new_size >= 2);
		assert (new_size <= size ());

		if (new_size == 2) {
			clear (resource);
			return;
		}

		stack[0] = new_size;
	}

	void shrink_to_fit (std::pmr::memory_resource &resource) {
		if (!stack || size () == capacity ())
			return;

		const unsigned old_size = size ();
		const unsigned old_cap = capacity ();

		int *new_stack = static_cast<int *> (resource.allocate (old_size * sizeof (int), alignof (int)));

		std::memcpy (new_stack, stack, old_size * sizeof (int));

		new_stack[1] = old_size;

		resource.deallocate (stack, old_cap * sizeof (int), alignof (int));

		stack = new_stack;
	}

	void clear (std::pmr::memory_resource &resource) {
		if (!stack)
			return;	
		const unsigned old_cap = capacity ();

		assert (old_cap >= 2);
    assert (stack[0] >= 2);
    assert (static_cast<unsigned>(stack[0]) <= old_cap);

		resource.deallocate (stack, old_cap * sizeof (int), alignof (int));

		stack = nullptr;
	}
	
	int *begin () {
	    return stack ? stack + 2 : nullptr;
	}

	const int *begin () const {
	    return stack ? stack + 2 : nullptr;
	}

	int *end () {
	    return stack ? stack + stack[0] : nullptr;
	}

	const int *end () const {
	    return stack ? stack + stack[0] : nullptr;
	}

	int &operator[] (uint32_t i) {
		assert (stack);
		assert (i < size () - 2);
		return stack[i + 2]; // skip size and capacity
	}

	const int &operator[] (uint32_t i) const {
		assert (stack);
		assert (i < size () - 2);
		return stack[i + 2];
	}
};

} // namespace CaDiCaL
#endif