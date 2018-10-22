/*  ===========================================================================
*
*   This file is part of HISE.
*   Copyright 2016 Christoph Hart
*
*   HISE is free software: you can redistribute it and/or modify
*   it under the terms of the GNU General Public License as published by
*   the Free Software Foundation, either version 3 of the License, or
*   (at your option) any later version.
*
*   HISE is distributed in the hope that it will be useful,
*   but WITHOUT ANY WARRANTY; without even the implied warranty of
*   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*   GNU General Public License for more details.
*
*   You should have received a copy of the GNU General Public License
*   along with HISE.  If not, see <http://www.gnu.org/licenses/>.
*
*   Commercial licenses for using HISE in an closed source project are
*   available on request. Please visit the project's website to get more
*   information about commercial licensing:
*
*   http://www.hise.audio/
*
*   HISE is based on the JUCE library,
*   which must be separately licensed for closed source applications:
*
*   http://www.juce.com
*
*   ===========================================================================
*/

namespace hlac { using namespace juce; 

HiseSampleBuffer::HiseSampleBuffer(HiseSampleBuffer& otherBuffer, int offset)
{
	if (otherBuffer.isFloatingPoint())
	{
		jassertfalse;
	}
	else
	{
		size = otherBuffer.size - offset;
		useOneMap = otherBuffer.useOneMap;

		leftIntBuffer = hlac::CompressionHelpers::getPart(otherBuffer.leftIntBuffer, offset, size);

		numChannels = otherBuffer.numChannels;

		if (numChannels > 1)
			rightIntBuffer = hlac::CompressionHelpers::getPart(otherBuffer.rightIntBuffer, offset, size);
	}
}


void HiseSampleBuffer::reverse(int startSample, int numSamples)
{
	if (isFloatingPoint())
	{
		floatBuffer.reverse(startSample, numSamples);

		int fadeLength = jmin<int>(500, numSamples);

		floatBuffer.applyGainRamp(numSamples - fadeLength, fadeLength, 1.0f, 0.0f);

	}
		
	else
	{

		leftIntBuffer.reverse(startSample, numSamples);

		if (numChannels > 1)
			rightIntBuffer.reverse(startSample, numSamples);

	}
		
}


void HiseSampleBuffer::setSize(int numChannels_, int numSamples)
{
	jassert(isPositiveAndBelow(numChannels, 3));



	numChannels = numChannels_;
	size = numSamples;

	if (isFloatingPoint())
		floatBuffer.setSize(numChannels, numSamples);
	else
	{
		leftIntBuffer = FixedSampleBuffer(numSamples);

		if (numChannels > 1)
			rightIntBuffer = FixedSampleBuffer(numSamples);
		else
			rightIntBuffer = FixedSampleBuffer(0);
	}
}

void HiseSampleBuffer::clear()
{
	if (isFloatingPoint())
		floatBuffer.clear();
	else
	{
		hlac::CompressionHelpers::IntVectorOperations::clear(leftIntBuffer.getWritePointer(), leftIntBuffer.size);

		if (hasSecondChannel())
			hlac::CompressionHelpers::IntVectorOperations::clear(rightIntBuffer.getWritePointer(), rightIntBuffer.size);
	}
}

void HiseSampleBuffer::clear(int startSample, int numSamples)
{
	if (numSamples <= 0)
		return;

	if (isFloatingPoint())
		floatBuffer.clear(startSample, numSamples);
	else
	{
		hlac::CompressionHelpers::IntVectorOperations::clear(leftIntBuffer.getWritePointer(startSample), numSamples);

		if (hasSecondChannel())
			hlac::CompressionHelpers::IntVectorOperations::clear(rightIntBuffer.getWritePointer(startSample), numSamples);
	}
}


void HiseSampleBuffer::allocateNormalisationTables(int offsetToUse)
{
	leftIntBuffer.getMap().setOffset(offsetToUse);
	leftIntBuffer.getMap().allocateTableIndexes(size + leftIntBuffer.getMap().getOffset());

	if (hasSecondChannel())
	{
		rightIntBuffer.getMap().setOffset(offsetToUse);
		rightIntBuffer.getMap().allocateTableIndexes(size + rightIntBuffer.getMap().getOffset());
	}

	normaliser.allocate(size);
}


void HiseSampleBuffer::flushNormalisationInfo(Range<int> rangeToFlush)
{
	//DBG("-- FLUSHING {" + String(rangeToFlush.getStart()) + ", " + String(rangeToFlush.getEnd()) + "}");

	auto& lMap = getNormaliseMap(0);
	auto& rMap = getNormaliseMap(useOneMap ? 0 : 1);

	auto offset = getNormaliseMap(0).getOffset();

	int numToFlush = jmin<int>(size, rangeToFlush.getLength());

	uint8 lastL = 128;
	uint8 lastR = 128;

	int currentStart = 0;
	int currentEnd = 0;

	int thisIndex = offset;

	while (numToFlush > 0)
	{
		int numThisTime = jmin<int>(numToFlush, 1024 - (thisIndex % 1024));

		//DBG("--- READING " + String(numThisTime) + " / " + String(numToFlush) + " at " + String(thisIndex));

		auto pos = lMap.getIndexForSamplePosition(thisIndex);

		uint8 nextL = lMap.getTableData()[pos];
		uint8 nextR = rMap.getTableData()[pos];

		currentEnd += numThisTime;
		numToFlush -= numThisTime;
		thisIndex += numThisTime;

		if (nextL != lastL || (useOneMap && nextR != lastR))
		{
			Normaliser::NormalisationInfo newInfo;

			newInfo.leftNormalisation = nextL;
			newInfo.rightNormalisation = useOneMap ? 0 : nextR;
			newInfo.range = rangeToFlush.getIntersectionWith({ currentStart, currentEnd });

			newInfo.range = newInfo.range;

			//DBG("---- ADDING RANGE (" + String(nextL) + ") {" + String(newInfo.range.getStart()) + ", " + String(newInfo.range.getEnd()) + "}");

			if (!newInfo.range.isEmpty())
				normaliser.infos.add(std::move(newInfo), true);

			lastL = nextL;
			lastR = nextR;

			currentStart = currentEnd;
		}
		else
		{
			auto* lastElement = normaliser.infos.end() - 1;
			lastElement->range.setEnd(currentEnd);

			//DBG("---- EXTENDING RANGE (" + String(nextL) + ") {" + String(lastElement.range.getStart()) + ", " + String(lastElement.range.getEnd()) + "}");

			currentStart = currentEnd;
		}
	}

	for (int i = 0; i < normaliser.infos.size(); i++)
	{
		const auto& info = normaliser.infos[i];

		if (info.leftNormalisation == 0 && (useOneMap || info.rightNormalisation == 0))
			normaliser.infos.remove(i--);
	}
}

hlac::FixedSampleBuffer& HiseSampleBuffer::getFixedBuffer(int channelIndex)
{
	jassert(!isFloatingPoint());

	if (channelIndex == 0)
	{
		return leftIntBuffer;
	}
	else
	{
		jassert(hasSecondChannel());
		return rightIntBuffer;
	}
}


void HiseSampleBuffer::convertToFloatWithNormalisation(float** data, int numChannels, int startSampleInSource, int numSamples) const
{
	Range<int> rangeInBuffer({ startSampleInSource, startSampleInSource + numSamples });

	if (useOneMap)
	{
		float* ptr = data[0];
		auto int_ptr = static_cast<const int16*>(getReadPointer(0, startSampleInSource));

		CompressionHelpers::fastInt16ToFloat(int_ptr, ptr, numSamples);

		normaliser.apply(ptr, nullptr, rangeInBuffer);

		if (numChannels == 2)
		{
			FloatVectorOperations::copy(data[1], data[0], numSamples);
		}
	}
	else
	{
		float* l_ptr = data[0];
		float* r_ptr = numChannels == 2 ? data[1] : nullptr;

		auto int_l_ptr = static_cast<const int16*>(getReadPointer(0, startSampleInSource));
		auto int_r_ptr = static_cast<const int16*>(getReadPointer(1, startSampleInSource));
		
		CompressionHelpers::fastInt16ToFloat(int_l_ptr, l_ptr, numSamples);
		
		if(numChannels == 2)
			CompressionHelpers::fastInt16ToFloat(int_r_ptr, r_ptr, numSamples);

		normaliser.apply(l_ptr, r_ptr, rangeInBuffer);
	}
}


void HiseSampleBuffer::clearNormalisation(Range<int> r)
{
	if (r.getStart() == 0 && r.getLength() == size)
		normaliser.clear();
	else
		normaliser.clear(r);
}


bool HiseSampleBuffer::usesNormalisation() const noexcept
{
	return normaliser.infos.size() != 0;
}

void HiseSampleBuffer::copyNormalisationRanges(const HiseSampleBuffer& otherBuffer, int startOffsetInBuffer)
{
	Range<int> rangeToClear({ startOffsetInBuffer, startOffsetInBuffer + otherBuffer.size });

	clearNormalisation(rangeToClear);

	for (const auto& i : otherBuffer.normaliser.infos)
	{
		Normaliser::NormalisationInfo i_copy(i);

		i_copy.range = i_copy.range + startOffsetInBuffer;

		normaliser.infos.add(std::move(i_copy), true);
	}

}

static int dummy = 0;

void HiseSampleBuffer::copy(HiseSampleBuffer& dst, const HiseSampleBuffer& source, int startSampleDst, int startSampleSource, int numSamples)
{
	if (numSamples <= 0)
		return;

	if (source.isFloatingPoint() == dst.isFloatingPoint())
	{
		if (source.isFloatingPoint())
		{
			auto byteToCopy = sizeof(float) * numSamples;

			memcpy(dst.getWritePointer(0, startSampleDst), source.getReadPointer(0, startSampleSource), byteToCopy);

			if (dst.hasSecondChannel())
			{
				if (source.hasSecondChannel())
					memcpy(dst.getWritePointer(1, startSampleDst), source.getReadPointer(1, startSampleSource), byteToCopy);
				else
					memcpy(dst.getWritePointer(1, startSampleDst), source.getReadPointer(0, startSampleSource), byteToCopy);
			}
		}
		else
		{
			auto byteToCopy = sizeof(int16) * numSamples;

			memcpy(dst.getWritePointer(0, startSampleDst), source.getReadPointer(0, startSampleSource), byteToCopy);

			if (dst.hasSecondChannel())
			{
				if (source.hasSecondChannel())
					memcpy(dst.getWritePointer(1, startSampleDst), source.getReadPointer(1, startSampleSource), byteToCopy);
				else
					memcpy(dst.getWritePointer(1, startSampleDst), source.getReadPointer(0, startSampleSource), byteToCopy);
			}

			Range<int> srcRange({ startSampleSource, startSampleSource + numSamples });
			Range<int> dstRange({ startSampleDst, startSampleDst + numSamples });

			dst.normaliser.copyFrom(source.normaliser, srcRange, dstRange);

			

#if 0

			auto o1 = (dst.getNormaliseMap(0).getOffset() + startSampleDst) % COMPRESSION_BLOCK_SIZE;
			auto o2 = (source.getNormaliseMap(0).getOffset() + startSampleSource) % COMPRESSION_BLOCK_SIZE;

			

			dst.setUseOneMap(source.useOneMap);
				
			if ((o1 == o2))
			{
				dst.getNormaliseMap(0).copyIntBufferWithNormalisation(source.getNormaliseMap(0), (const int16*)source.getReadPointer(0), (int16*)dst.getWritePointer(0, 0), startSampleSource, startSampleDst, numSamples, true);

				if (dst.hasSecondChannel() && !dst.useOneMap)
				{
					dst.getNormaliseMap(1).copyIntBufferWithNormalisation(source.getNormaliseMap(1), (const int16*)source.getReadPointer(1), (int16*)dst.getWritePointer(1, 0), startSampleSource, startSampleDst, numSamples, true);
				}

				dummy++;

				//AudioThreadGuard::Suspender ss;
				//CompressionHelpers::dump(dst.getFixedBuffer(0), "Merge" + String(dummy) + ".wav");

			}
			else
			{
				equaliseNormalisationAndCopy(dst, source, startSampleDst, startSampleSource, numSamples, 0);

				if (dst.hasSecondChannel())
				{
					equaliseNormalisationAndCopy(dst, source, startSampleDst, startSampleSource, numSamples, 1);
					

				}
			}
#endif
			
		}
	}
	else
	{
		// Data type mismatch!
		jassertfalse;
	}
}

void HiseSampleBuffer::add(HiseSampleBuffer& dst, const HiseSampleBuffer& source, int startSampleDst, int startSampleSource, int numSamples)
{
	if (numSamples <= 0)
		return;

	if (source.isFloatingPoint() && dst.isFloatingPoint())
	{
		dst.floatBuffer.addFrom(0, startSampleDst, source.floatBuffer, 0, startSampleSource, numSamples);

		if (dst.hasSecondChannel())
		{
			if (source.hasSecondChannel())
				dst.floatBuffer.addFrom(1, startSampleDst, source.floatBuffer, 1, startSampleSource, numSamples);
			else
				dst.floatBuffer.addFrom(1, startSampleDst, source.floatBuffer, 0, startSampleSource, numSamples);
		}
	}
	else if (!source.isFloatingPoint() && !dst.isFloatingPoint())
	{
		auto ld = dst.leftIntBuffer.getWritePointer(startSampleDst);
		auto ls = source.leftIntBuffer.getReadPointer(startSampleSource);

		CompressionHelpers::IntVectorOperations::add(ld, ls, numSamples);

		if (dst.hasSecondChannel())
		{
			if (source.hasSecondChannel())
			{
				auto ld2 = dst.rightIntBuffer.getWritePointer(startSampleDst);
				auto ls2 = source.rightIntBuffer.getReadPointer(startSampleSource);

				CompressionHelpers::IntVectorOperations::add(ld2, ls2, numSamples);
			}
			else
			{
				auto ld2 = dst.rightIntBuffer.getWritePointer(startSampleDst);
				auto ls2 = source.leftIntBuffer.getReadPointer(startSampleSource);

				CompressionHelpers::IntVectorOperations::add(ld2, ls2, numSamples);
			}
		}
	}
	else
	{
		// Data type mismatch!
		jassertfalse;
	}
}

void* HiseSampleBuffer::getWritePointer(int channel, int startSample)
{
	if (isFloatingPoint())
	{
		return floatBuffer.getWritePointer(channel, startSample);
	}
	else
	{
		if (channel == 0)
			return leftIntBuffer.getWritePointer(startSample);
		else if (channel == 1 && hasSecondChannel())
		{
			return rightIntBuffer.getWritePointer(startSample);
		}
		else
		{
			jassertfalse;
			return nullptr;
		}
	}
}

const void* HiseSampleBuffer::getReadPointer(int channel, int startSample/*=0*/) const
{
	if (isFloatingPoint())
	{
		return floatBuffer.getReadPointer(channel % numChannels, startSample);
	}
	else
	{
		if (channel == 0 || numChannels == 1)
			return leftIntBuffer.getReadPointer(startSample);
		else if (channel == 1 && hasSecondChannel())
		{
			return rightIntBuffer.getReadPointer(startSample);
		}
		else
		{
			jassertfalse;
			return nullptr;
		}
	}
}

void HiseSampleBuffer::applyGainRamp(int channelIndex, int startOffset, int rampLength, float startGain, float endGain)
{
	if (isFloatingPoint())
	{
		floatBuffer.applyGainRamp(channelIndex, startOffset, rampLength, startGain, endGain);
	}
	else
	{
		if(channelIndex == 0)
			leftIntBuffer.applyGainRamp(startOffset, rampLength, startGain, endGain);

		if(channelIndex == 1 && hasSecondChannel())
			rightIntBuffer.applyGainRamp(startOffset, rampLength, startGain, endGain);
	}
}

AudioSampleBuffer* HiseSampleBuffer::getFloatBufferForFileReader()
{
	jassert(isFloatingPoint());

	return &floatBuffer;
}

const hlac::CompressionHelpers::NormaliseMap& HiseSampleBuffer::getNormaliseMap(int channelIndex) const
{
	return (channelIndex == 0) ? leftIntBuffer.getMap() : rightIntBuffer.getMap();
}

hlac::CompressionHelpers::NormaliseMap& HiseSampleBuffer::getNormaliseMap(int channelIndex)
{
	return (channelIndex == 0) ? leftIntBuffer.getMap() : rightIntBuffer.getMap();
}


void HiseSampleBuffer::minimizeNormalisationInfo()
{
	for (int i = 0; i < normaliser.infos.size()-1; i++)
	{
		auto& ci = normaliser.infos.getReference(i);
		const auto& ni = normaliser.infos[i + 1];

		const bool levelMatch = ci.leftNormalisation == ni.leftNormalisation && (useOneMap || ci.leftNormalisation == ci.rightNormalisation);

		const bool neighbours = ci.range.getEnd() == ni.range.getStart();

		if (levelMatch && neighbours)
		{
			ci.range.setEnd(ni.range.getEnd());
			normaliser.infos.remove(i + 1);
			i--;
		}
	}
}

void HiseSampleBuffer::Normaliser::copyFrom(const Normaliser& source, Range<int> srcRange, Range<int> dstRange)
{
	int offset = dstRange.getStart() - srcRange.getStart();
	

	for (auto& i : infos)
	{
		if (i.range.intersects(dstRange))
		{
			// This range needs to be cut by the dstRange amount.
			if (dstRange.contains(i.range))
			{
				// will be deleted later
				i.leftNormalisation = 0;
				i.rightNormalisation = 0;
			}
			else if (i.range.contains(dstRange))
			{
				if (i.leftNormalisation == 0 && i.rightNormalisation == 0)
					continue;
				else
				{
					i.range.setEnd(dstRange.getStart());

					NormalisationInfo newRange;
					newRange.leftNormalisation = i.leftNormalisation;
					newRange.rightNormalisation = i.rightNormalisation;

					newRange.range.setStart(dstRange.getEnd());
					newRange.range.setEnd(i.range.getEnd());

					infos.add(std::move(newRange), true);
				}
			}
			else if (dstRange.contains(i.range.getStart() - 1))
			{
				i.range.setStart(dstRange.getEnd());
			}
			else if (dstRange.contains(i.range.getEnd() - 1))
			{
				i.range.setEnd(dstRange.getStart());
			}
			else
				jassertfalse;
		}
	}

	for (const auto& i : source.infos)
	{
		if (i.leftNormalisation == 0 && i.rightNormalisation == 0)
			continue;

		auto thisRange = srcRange.getIntersectionWith(i.range);

		if (!thisRange.isEmpty())
		{
			NormalisationInfo newInfo;
			newInfo.leftNormalisation = i.leftNormalisation;
			newInfo.rightNormalisation = i.rightNormalisation;
			newInfo.range = thisRange + offset;

			infos.add(std::move(newInfo), true);
		}
	}
}

} // namespace hlac
