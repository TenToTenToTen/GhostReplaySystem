/*
* Copyright 2025 TenToTen, All Rights Reserved.
*/


#pragma once

#include "CoreMinimal.h"
#include "BloodStainFileOptions.generated.h"

/** Supported Compression Method */
UENUM(BlueprintType)
enum class ECompressionMethod : uint8
{
	None     UMETA(DisplayName="None"),
	Zlib     UMETA(DisplayName="Zlib"),
	Gzip     UMETA(DisplayName="Gzip"),
	LZ4      UMETA(DisplayName="LZ4"),
};

/** 압축 관련 세부 옵션 */
USTRUCT(BlueprintType)
struct FCompressionOption
{
	GENERATED_BODY()

	/** 사용할 압축 메서드 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Compression")
	ECompressionMethod Method = ECompressionMethod::Zlib;

	/** 압축 레벨 (0=none, 1~9 high) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Compression", meta=(ClampMin="0",ClampMax="9"))
	int32 Level = 6;

	friend FArchive& operator<<(FArchive& Ar, FCompressionOption& Options);
};

// 어떤 양자화 전략을 사용할지 선택하는 열거형
UENUM(BlueprintType)
enum class ETransformQuantizationMethod : uint8
{
	None,                // 양자화 안함 (FTransform, 48바이트)
	Standard_High,       // 표준 양자화 (FQuatFixed48 사용, 약 20바이트)
	Standard_Compact,    // 압축 양자화 (FQuatFixed32 사용, 약 18바이트)
	Standard_Low,      // 저해상도 양자화 (86비트, 약 12바이트)
};

// 양자화 옵션을 담는 구조체
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

/** 파일 저장·로드를 제어하는 최상위 옵션 묶음 */
USTRUCT(BlueprintType)
struct FBloodStainFileOptions
{
	GENERATED_BODY()

	/** 압축 옵션 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="File|Compression")
	FCompressionOption Compression;

	/** 본 트랜스폼 퀀타이제이션 옵션 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="File|Quantization")
	FQuantizationOption Quantization;

	/** 파일 끝 CRC 체크섬을 포함할지 여부 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="File|Integrity")
	bool bIncludeChecksum = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="File|Quantization")
	bool bUseNetQuantize = true;
	
	/** 암호화(Encrypt) 옵션 – 추후 확장 가능 */
	// UPROPERTY(...)
	// FEncryptionOptions Encryption;
	
	friend FArchive& operator<<(FArchive& Ar, FBloodStainFileOptions& Options);
};

USTRUCT()
struct FBloodStainFileHeader
{
    GENERATED_BODY()

    /** 매직 넘버 + 버전 체크 */
    uint32 Magic = 0x5253746E; // 'RStn'
    uint32 Version = 1;

    /** 압축/퀀타이즈 옵션을 그대로 저장 */
    UPROPERTY()
    FBloodStainFileOptions Options;

	/** 압축 페이로드의 원본(비압축) 크기 */
	int64 UncompressedSize = 0;

    friend FArchive& operator<<(FArchive& Ar, FBloodStainFileHeader& Header);
};