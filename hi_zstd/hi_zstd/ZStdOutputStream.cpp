/*  ===========================================================================

BSD License

For Zstandard software

Copyright (c) 2016-present, Facebook, Inc. All rights reserved.

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

* Redistributions of source code must retain the above copyright notice, this
list of conditions and the following disclaimer.

* Redistributions in binary form must reproduce the above copyright notice,
this list of conditions and the following disclaimer in the documentation
and/or other materials provided with the distribution.

* Neither the name Facebook nor the names of its contributors may be used to
endorse or promote products derived from this software without specific
prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

*   ===========================================================================
*/

namespace zstd {
using namespace juce;

struct ZstdOutputStream::Pimpl
{
	Pimpl(OutputStream* ownedOutput, int compressionLevel) :
		output(ownedOutput)
	{
		zstd_streamObject = ZSTD_createCStream();
		ZSTD_initCStream(zstd_streamObject, compressionLevel);

		currentBufferSize = ZSTD_CStreamInSize();
		outBufferData.allocate(currentBufferSize, true);
		inBufferData.allocate(currentBufferSize, true);

		outBuffer.dst = outBufferData;
		outBuffer.size = currentBufferSize;
		outBuffer.pos = 0;

		inBuffer.src = inBufferData;
		inBuffer.size = currentBufferSize;
		inBuffer.pos = 0;
	}

	~Pimpl()
	{
		ZSTD_freeCStream(zstd_streamObject);

		output->flush();
		output = nullptr;
	}

	bool write(const void* dataToWrite, size_t numberOfBytes)
	{
		memcpy(inBufferData.getData(), dataToWrite, numberOfBytes);

		inBuffer.pos = 0;
		inBuffer.size = numberOfBytes;

		auto lastPos = outBuffer.pos;
		auto result = ZSTD_compressStream(zstd_streamObject, &outBuffer, &inBuffer);

		DictionaryHelpers::checkResult(result);

		result = ZSTD_flushStream(zstd_streamObject, &outBuffer);

		DictionaryHelpers::checkResult(result);
		auto numBytesWritten = outBuffer.pos - lastPos;

		totalPosition += numBytesWritten;
		return output->write(outBufferData.getData() + lastPos, numBytesWritten);
	}

	void flush()
	{
		ZSTD_endStream(zstd_streamObject, &outBuffer);
	}

	HeapBlock<uint8> outBufferData;
	HeapBlock<uint8> inBufferData;

	size_t currentBufferSize = 0;

	int totalPosition = 0;

	ZSTD_inBuffer inBuffer;
	ZSTD_outBuffer outBuffer;

	ScopedPointer<OutputStream> output;

	ZSTD_CStream* zstd_streamObject;
};


ZstdOutputStream::ZstdOutputStream(OutputStream* ownedOutputStream, int compressionLevel /*= 5*/) :
	pimpl(new Pimpl(ownedOutputStream, compressionLevel))
{}

ZstdOutputStream::~ZstdOutputStream()
{
	pimpl = nullptr;
}

void ZstdOutputStream::flush() { pimpl->flush(); }
bool ZstdOutputStream::setPosition(int64 newPosition) { return false; }
juce::int64 ZstdOutputStream::getPosition() { return pimpl->output->getPosition(); }
bool ZstdOutputStream::write(const void* dataToWrite, size_t numberOfBytes) { return pimpl->write(dataToWrite, numberOfBytes); }

}