/*
* Copyright 2025 TenToTen, All Rights Reserved.
*/


#pragma once

#include "CoreMinimal.h"
#include "BloodStainFileOptions.generated.h"

/**
 * @brief Supported compression algorithms
 */
UENUM(BlueprintType)
enum class ECompressionMethod : uint8
{
	None  UMETA(DisplayName = "None"),
	Zlib  UMETA(DisplayName = "Zlib"),
	Gzip  UMETA(DisplayName = "Gzip"),
	LZ4   UMETA(DisplayName = "LZ4")  
};

/**
 * @brief Detailed compression settings
 */
USTRUCT(BlueprintType)
struct FCompressionOption
{
	GENERATED_BODY()

	/** Compression algorithm to use */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Compression")
	ECompressionMethod Method = ECompressionMethod::Zlib;

	friend FArchive& operator<<(FArchive& Ar, FCompressionOption& Options);
};


/**
 * @brief Supported transform quantization methods.
 *
 * - None: No quantization (stores full FTransform).
 * - Standard_High: High‑precision quantization (uses FQuantizedTransform_High).
 * - Standard_Compact: Compact quantization (uses FQuantizedTransform_Compact).
 * - Standard_Low: Lowest‑bit quantization (uses FQuantizedTransform_Lowest).
 */
UENUM(BlueprintType)
enum class ETransformQuantizationMethod : uint8
{
	None,            
	Standard_High,   
	Standard_Compact,
	Standard_Low     
};

/**
 * @brief Detailed quantization settings
 */
USTRUCT(BlueprintType)
struct FQuantizationOption
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Quantization")
	ETransformQuantizationMethod Method = ETransformQuantizationMethod::Standard_Compact;

	friend FArchive& operator<<(FArchive& Ar, FQuantizationOption& Options)
	{
		Ar << Options.Method;
		return Ar;
	}
};

/**
 * @brief High-level file I/O options for BloodStain recordings
 */
USTRUCT(BlueprintType)
struct FBloodStainFileOptions
{
	GENERATED_BODY()

	/** Compression settings for file payload */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="File|Compression")
	FCompressionOption Compression;

	/** Quantization settings for bone transforms */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="File|Quantization")
	FQuantizationOption Quantization;
	
	friend FArchive& operator<<(FArchive& Ar, FBloodStainFileOptions& Options);
};

/**
 * @brief Header prepended to all BloodStain data files
 */
USTRUCT()
struct FBloodStainFileHeader
{
    GENERATED_BODY()

	/** [Unused] Magic identifier ('RStn') and version */
    uint32 Magic = 0x5253746E;
    uint32 Version = 1;

	/** File I/O options */
    UPROPERTY()
    FBloodStainFileOptions Options;

	/** Size of the uncompressed payload in bytes */
	int64 UncompressedSize = 0;

    friend FArchive& operator<<(FArchive& Ar, FBloodStainFileHeader& Header);
};