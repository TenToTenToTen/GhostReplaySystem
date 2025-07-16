/*
* Copyright 2025 TenToTen, All Rights Reserved.
*/

#pragma once
#include "CoreMinimal.h"
#include "BloodStainFileOptions.h"

namespace BloodStainCompressionUtils
{
	/** 
	 * Compress InBuffer by Options.Compression.Method, Options.Compression.Level 
	 * @return success/failure
	 */
	bool CompressBuffer(const TArray<uint8>& InBuffer,
							  TArray<uint8>& OutCompressed,
							  FCompressionOption Opts = FCompressionOption());

	/**
	 * Decompress the compressed data InBuffer to the original size (UncompressedSize)
	 * @param UncompressedSize The value of RawBuffer.Num(), measured right before saving, must be stored in the header or as a separate prefix.
	 * @return success/failure
	 */
	bool DecompressBuffer(const TArray<uint8>& Compressed,
								TArray<uint8>&	   OutRaw,
								int64	 UncompressedSize,
								FCompressionOption  Opts = FCompressionOption());
}
