// Developed by Noah Reeder
// Started on 2018-11-22
// RNGClass.h - This header declares the RNGClass class, and (due to it being a class template) implements it, as well as defining
//		two overloads of the SimpleRandom function

/* -*-*-*-*-*-*-*-*-*-*-*-*-*- NOTES -*-*-*-*-*-*-*-*-*-*-*-*-*-
- "SimpleRandom" has similar output to "rand" (however it will exceed RAND_MAX - its range is { RETURN ∈ int | 0 ≤ RETURN ≤ INT_MAX }).

- Although SimpleRandom is easy to use, it is extremely inefficient when compared to RNGClass usage. If large amounts of numbers
	are to be generated, I highly recommend using an RNGClass instance, although SimpleRandom will still operate as expected.

- Unlike "rand" these functions are all thread-safe.

- At the time of writing, RNGClass::FloatingRand will generate numbers inclusively. According to documentation on the
	std::uniform_real_distribution the maximum number is excluded, however my tests have demonstrated otherwise.
*/

/* -*-*-*-*-*-*-*-*-*-*-*- DOCUMENTATION -*-*-*-*-*-*-*-*-*-*-*-
To generate a simple random integer (akin to using "rand()"):
 call SimpleRandom()
   RETURN: int

To generate a simple random integer in a range:
 call SimpleRandom(floor, roof)
	 floor: int, the minimum possible number
	 roof: int, the maximum possible number
   RETURN: int
 NOTE: Generates numbers in the set { RETURN ∈ int | floor ≤ RETURN ≤ roof }

			   -------- Advanced Users Only --------
NOTE: Examples use "rng" as the identifier for the RNGClass instance, and result_type as the type specified at identifier declaration

To create an instance of the random number generation class
  declare RNGClass<result_type> identifier
	result_type: An *UNSIGNED* integral type to generate numbers in (e.g. "unsigned int", "unsigned long long")
	identifier: The name of the identifier used to access the class instance

To generate a random number using the RNGClass instance
 Call rng()
 ----------OR---------
 Call rng.operator()()
 ----------OR---------
 Call rng.GetRand()
 =====================
   RETURN: result_type

To generate a random number in a specified range using the RNGClass instance
 Call rng(floor, roof)
 ----------OR---------
 Call rng.operator()(floor, roof)
 ----------OR---------
 Call rng.GetRand(floor, roof)
 =====================
	 floor: result_type, the minimum possible number
	 roof: result_type, the maximum possible number
   RETURN: result_type
   NOTE: Generates numbers in the set { RETURN ∈ result_type | floor ≤ RETURN ≤ roof }

To generate a random number of an integral type different than result_type
 Call rng.CustomRand<cast_type>(floor, roof)
	 cast_type: the type of the result (e.g. int, long long, unsigned int)
	 floor: cast_type, the minimum possible number. If omitted, it becomes the type's lowest possible value (e.g. INT_MIN)
	 roof: cast_type, the maximum possible number. If omitted, it becomes the type's largest possible value (e.g. INT_MAX)
   RETURN: cast_type
   NOTE: Generates numbers in the set { RETURN ∈ cast_type | floor ≤ RETURN ≤ roof }

To generate a random number of a floating-point type
 Call rng.FloatingRand<floating_type>(floor, roof)
	 floating_type: the type of the result (e.g. float, long double)
	 floor: floating_type, the minimum possible number. If omitted, it becomes 0
	 roof: floating_type, the maximum possible number. If omitted, it becomes 1
   RETURN: floating_type
   NOTE: ****IMPORTANT**** See point 4 in above notes segment

To manually initialize the RNGClass instance
 Call rng.Initialize(reinitialize)
	 reinitialize: bool, whether or not to reinitialize if the class instance is already initialized. If omitted, it becomes false
   RETURN: void
*/

// Include guard
#ifndef RNGCLASS_H
#define RNGCLASS_H

// If necessary, include the header to allow the use of WinAPI
#ifndef _WINDOWS_
#include <Windows.h>
#endif
// If necessary, load the BCrypt API
// NOTE: Other headers may use bcrypt.h but not include the library, and since I have verified that the generated assembly
//		of a program is identical regardless of the number of times a specific library is included  (i.e. if you use "#pragma
//		comment(lib, "bcrypt.lib")" 50 times or 1 time, the generated code is the same), we will include the library regardless
//		of its current inclusion status
#pragma comment(lib, "bcrypt.lib")
#ifndef __BCRYPT_H__
#include <bcrypt.h>
#endif
// If necessary, include the header to allow strings
#ifndef _STRING_
#include <string>
#endif
// If necessary, include the header to allow random number generators
#ifndef _RANDOM_
#include <random>
#endif
// If necessary, include the header to define limits of integral types
#ifndef _LIMITS_
#include <limits>
#endif
// If necessary, include the header to allow the use of mutex to ensure thread-safety
#ifndef _MUTEX_
#include <mutex>
#endif
// If necessary, include the header to allow the use of condition_variable to ensure thread-safety
#ifndef _CONDITION_VARIABLE_
#include <condition_variable>
#endif
// Include the header to allow run-time assertions. NOTE: assert.h does not contain an include guard, but due to only containing
//		a forward declaration and a macro there are no side effects of multiple inclusions
#include <assert.h>

// typename T; // The type of number to generate. Must be unsigned
template <typename T>
class RNGClass { // NOTE: Most of the stuff in this class isn't done in my usual style in order for the class to be compliant
				 //		with §29.6.1.3 of the C++17 standard draft, which is necessary to be used with the std::shuffle function
public:
	// Ensure that the provided type is numerical and unsigned
	static_assert(std::is_unsigned_v<T>, "The type provided for RNGClass must be unsigned");

	// Create result_type as an alias for T (the provided type)
	typedef T result_type;

	// Define the default constructor
	RNGClass() : initialized(false), dying(false), pending_count(0) {}

	// Define the destructor
	~RNGClass() {
		// std::unique_lock<std::mutex> lock;	// The lock used to wait for number generations to complete. Declared later due to
		//		constructor use
		std::condition_variable condition;		// The condition used to block until number generations complete

		// Disallow new generations
		this->dying = true;

		// Create a lock and wait until there are no pending number generations
		std::unique_lock<std::mutex> lock(this->destruction_muter);
		condition.wait(lock, [this]()->bool { return this->pending_count == 0; });

		// Denote that the generator is no longer initialized
		this->initialized = false;

		// Release the handle of the RNG algorithm
		BCryptCloseAlgorithmProvider(this->algorithm_handle, NULL);
	}
	// End RNGClass<T>::~RNGClass method

	// **** Define non-const methods ****

	// Define the () operator to return a random number of the provided type in the set
	//		{ number ∈ result_type | min() ≤ number ≤ max() }, as required by §29.6.1.3 of the C++17 standard draft
	result_type operator()() {
		result_type number; // The number to return

		// Increment the number of pending generations, forwarding exceptions
		try { this->IncrementCount(); }
		catch (...) { throw; }

		// If necessary, initialize this instance of RNGClass
		if (!initialized) { this->Initialize(); }

		// Calculate the random number, and return it
		BCryptGenRandom(this->algorithm_handle, (unsigned char*)&number, sizeof(number), NULL);

		// Decrement the number of pending generations
		this->DecrementCount();

		// Return the generated number
		return number;
	}
	// End RNGClass<T>::operator() [overload: void] method

	// Define an overload of the () operator to return a random number of in the set
	//		{ number ∈ result_type | floor ≤ number ≤ roof }
	result_type operator()(result_type floor, result_type roof) {
		// result_type floor;	// The minimum number that can be returned. Passed
		// result_type roof;	// The maximum number that can be returned. Passed
		result_type number;		// The number to return

		// Ensure that the floor is lower than the roof, embedding an error message for if the assertion fails using the comma operator
		assert(("Lower bound is greater than upper bound. Check for implicit casting?", floor < roof));

		// Increment the number of pending generations, forwarding exceptions
		try { this->IncrementCount(); }
		catch (...) { throw; }

		// Create a temporary integer distributer and use it to get a number within the specified range of the specified type
		number = std::uniform_int_distribution<result_type>(floor, roof)(*this);

		// Decrement the number of pending generations
		this->DecrementCount();

		// Return the generated number
		return number;
	}
	// End RNGClass<T>::operator() [overload: result_type, result_type] method

	// Define a method to intitialize the instance of RNGClass
	void Initialize(bool reinitialize = false) {
		// bool reinitialize; // Boolean for whether or not to reinitialize the class instance if the class instance has already
		//		been initialized. Passed. False by default

		// Check if the class instance is already initialized, dealing with reinitialization as specified in the function call
		if (initialized) {
			if (reinitialize) {
				// Prepare to reinitialize
				initialized = false;
				BCryptCloseAlgorithmProvider(&this->algorithm_handle, NULL);
			} // End if(reinitialize)
			else { return; } // If no reinitialization is wanted, don't do anything
		} // End if(initialized)

		// Get the handle to the RNG algorithm
		BCryptOpenAlgorithmProvider(&this->algorithm_handle, BCRYPT_RNG_ALGORITHM, NULL, NULL);
		initialized = true;
	}
	// End RNGClass<T>::Initialize method

	// Define the method "max" to return the maximum number the provided type can contain. NOTE: Name is wrapped in "()" to
	//		avoid the compiler trying to replace "max" with the macro defined in Windows.h
	static T(max)() { return (std::numeric_limits<T>::max)(); }

	// Define the method "min" to return the minimum number the provided type can contain (0 because the types must be unsigned)
	static T(min)() { return 0; }

	// Define a method to return a random number (since pointers can't access the "()" operator in an easily readable way)
	result_type GetRand() { try { return this->operator()(); } catch (...) { throw; } }

	// Define an overload of GetRand to return a random number of the specified type in the specified range
	result_type GetRand(result_type floor, result_type roof) { try { return this->operator()(floor, roof); } catch (...) { throw; } }

	// Define a templated method to generate a random number of the specified type over the specified range
	//		{ number ∈ cast_type | floor ≤ number ≤ roof }
	template<typename cast_type> cast_type CustomRand(cast_type floor = (std::numeric_limits<cast_type>::min)(), cast_type roof = (std::numeric_limits<cast_type>::max)()) {
		// cast_type floor; // The minimum number that can be returned. Passed. Minimum of the type if omitted
		// cast_type roof;	// The maximum number that can be returned. Passed. Maximum of the type if omitted
		cast_type number;	// The number to return

		// Ensure that the provided type is numerical, but not necessarily and unsigned
		static_assert(std::is_integral_v<cast_type>, "The type provided for RNGClass::CustomRand must be integral");

		// Ensure that the floor is lower than the roof, embedding an error message for if the assertion fails using the comma operator
		assert(("Lower bound is greater than upper bound. Check for implicit casting?", floor < roof));

		// Increment the number of pending generations, forwarding exceptions
		try { this->IncrementCount(); }
		catch (...) { throw; }

		// Create a temporary integer distributer and use it to get a number within the specified range of the specified type
		number = std::uniform_int_distribution<cast_type>(floor, roof)(*this);

		// Decrement the number of pending generations
		this->DecrementCount();

		// Return the generated number
		return number;
	}
	// End RNGClass<T>::CustomRand<cast_type> method

	// Define a templated method to generate a random floating-point number of the specified type over the specified range
	template<typename floating_type> floating_type FloatingRand(floating_type floor = 0, floating_type roof = 1) {
		// cast_type floor;		// The minimum number that can be returned. Passed. 0 if omitted
		// cast_type roof;		// The maximum number that can be returned. Passed. 1 if omitted
		floating_type number;	// The number to return

		// Ensure that the provided type is floating-point
		static_assert(std::is_floating_point_v<floating_type>, "The type provided for RNGClass::FloatingRand must be floating-point");

		// Ensure that the floor is lower than the roof, embedding an error message for if the assertion fails using the comma operator
		assert(("Lower bound is greater than upper bound. Check for implicit casting?", floor < roof));

		// Increment the number of pending generations, forwarding exceptions
		try { this->IncrementCount(); }
		catch (...) { throw; }

		// Create a temporary integer distributer and use it to get a number within the specified range of the specified type
		number = std::uniform_real_distribution<floating_type>(floor, roof)(*this);

		// Decrement the number of pending generations
		this->DecrementCount();

		// Return the generated number
		return number;
	}
	// End RNGClass<T>::FloatingRand<floating_type> method

protected:
	// Define a method to increment the number of pending generations
	void IncrementCount() {
		// std::lock_guard<std::mutex> lock;	// The lock used to ensure that the incrementation of the pending number
		//		generations is not interrupted. Declared later due to constructor use

		// Check if new generations are allowed
		if (this->dying) {
#if _DEBUG // If debugging mode enabled, create a message box before throwing exception
			thread_local bool shown = false; // NOTE: Only appears in debugging mode
			if (!shown) { // Only show message box once *PER THREAD* (but throw exception appropriate number of times)
				MessageBox(NULL, TEXT("Destruction scheduled - no additional generations allowed. NOTE: Will appear once per thread)"), TEXT("RNGClass error"), MB_OK);
				shown = true;
			}
#endif
			// Throw an exception
			throw std::exception("RNG called after destruction scheduled");
		}

		// If necessary, block the current thread until current count incrementation is complete
		std::lock_guard<std::mutex> lock(this->count_muter);

		// Incremet count
		pending_count += 1; // NOTE: lock is released when it runs out of scope
	}
	// End RNGClass<T>::IncrementCount method

	// Define a method to decrement the number of pending generations
	void DecrementCount() {
		// std::lock_guard<std::mutex> lock;	// The lock used to ensure that the decrementation of the pending number
		//		generations is not interrupted. Declared later due to constructor use

		// If necessary, block the current thread until current count decrementation is complete
		std::lock_guard<std::mutex> lock(this->count_muter);

		// Decremet count
		pending_count -= 1; // NOTE: lock is released when it runs out of scope
	}
	// End RNGClass<T>::DecrementCount method

	bool initialized;					// Boolean for whether or not the instance is initialized
	BCRYPT_ALG_HANDLE algorithm_handle;	// The handle to the algorithm used for generating numbers (time intensive to get)
	// NOTE: variables regarding thread safety are private to prevent tampering
private:
	bool dying;							// Boolean for whether or not the class instance is trying to be destroyed (used to deny generations)
	unsigned long long pending_count;	// The number of pending generations
	std::mutex count_muter;				// The mutex used to block threads during modification of pending_count
	std::mutex destruction_muter;		// The mutex used to block the class instance from being prematurely destroyed
}; // End class RNGClass

// Note: Functions are static because I don't want to bother including a seperate cpp file just for two tiny functions, however
//		they still need to be available across translation units

// Define the function to use a static RNGClass instance to return a single random number
static int SimpleRandom() {
	static RNGClass<unsigned int> rng; // Random number generator used in this function across lifetime of the program
	return std::abs((int)rng());
}
// End SimpleRandom [overload: void] function

// Define the overload of SimpleRandom to be used to generate a number over the range { number ∈ int | minimum ≤ number ≤ maximum }
static int SimpleRandom(int floor, int roof) {
	// int floor;	// The minimum number that can be produced. Passed
	// int roof;	// The maximum number that can be produced. Passed
	static RNGClass<unsigned int> rng; // Random number generator used in this function across lifetime of the program
	return std::uniform_int_distribution<int>(floor, roof)(rng);
}
// End SimpleRandom [overload: int, int] function
#endif
