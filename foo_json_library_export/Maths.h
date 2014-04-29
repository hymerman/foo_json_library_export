#pragma once

namespace maths {

//------------------------------------------------------------------------------

template<typename T>
T
clip(
	const T&		value,
	const T&		min,
	const T&		max
)
{
	if(value < min) return min;
	else if(value > max) return max;
	else return value;
}

//------------------------------------------------------------------------------

template<typename T>
T
lerp(
	const T&	a,
	const T&	b,
	const float t
)
{
	return a * (1.0f - t) + b * t;
}

//------------------------------------------------------------------------------

/// Maps value from the range [inputRangeA, inputRangeB] to the range [outputRangeA, outputRangeB].
template<typename OutputType, typename InputType>
OutputType
map(
	const InputType			value,
	const InputType			inputRangeA,
	const InputType			inputRangeB,
	const OutputType		outputRangeA,
	const OutputType		outputRangeB
)
{
	if(inputRangeA == inputRangeB)
	{
		return outputRangeA;
	}

	const float proportionOfInputRange = static_cast<float>(value - inputRangeA) / static_cast<float>(inputRangeB - inputRangeA);
	const float clippedProportion = clip(proportionOfInputRange, 0.0f, 1.0f);

	return lerp(outputRangeA, outputRangeB, clippedProportion);
}

//------------------------------------------------------------------------------

/// Maps value from the range [inputRangeA, inputRangeB] to the range [0.0f, 1.0f].
template<typename InputType>
float
mapToUnary(
	const InputType		value,
	const InputType		inputRangeA,
	const InputType		inputRangeB
)
{
	return map(value, inputRangeA, inputRangeB, 0.0f, 1.0f);
}

//------------------------------------------------------------------------------

} // namespace maths
