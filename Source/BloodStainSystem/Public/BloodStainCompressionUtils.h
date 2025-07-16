/*
* Copyright 2025 TenToTen, All Rights Reserved.
*/

#pragma once
#include "CoreMinimal.h"
#include "BloodStainFileOptions.h"

namespace BloodStainCompressionUtils
{
	/** 
	 * InBuffer 를 Options.Compression.Method, Options.Compression.Level 에 따라 압축합니다. 
	 * @return 성공/실패
	 */
	bool CompressBuffer(const TArray<uint8>& InBuffer,
							  TArray<uint8>& OutCompressed,
						const FCompressionOption& Opts);

	/**
	 * 압축된 데이터 InBuffer 를 원본 크기(UncompressedSize)만큼 해제합니다.
	 * UncompressedSize 는 Save 직전에 측정한 RawBuffer.Num() 을 헤더나 별도 prefix에 저장해야 합니다.
	 */
	/** CompressBuffer/DecompressBuffer */
	bool DecompressBuffer(const TArray<uint8>& Compressed,
								TArray<uint8>&	   OutRaw,
						  const FCompressionOption&  Opts,
								int64	 UncompressedSize);
}
