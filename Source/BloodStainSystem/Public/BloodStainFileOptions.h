// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "BloodStainFileOptions.generated.h"

/** Supported Compression Method */
UENUM(BlueprintType)
enum class EBSFCompressionMethod : uint8
{
	None     UMETA(DisplayName="None"),
	Zlib     UMETA(DisplayName="Zlib"),
	Gzip     UMETA(DisplayName="Gzip"),
	LZ4      UMETA(DisplayName="LZ4"),
};

/** 압축 관련 세부 옵션 */
USTRUCT(BlueprintType)
struct FBSFCompressionOptions
{
	GENERATED_BODY()

	/** 사용할 압축 메서드 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Compression")
	EBSFCompressionMethod Method = EBSFCompressionMethod::Zlib;

	/** 압축 레벨 (0=none, 1~9 high) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Compression", meta=(ClampMin="0",ClampMax="9"))
	int32 Level = 6;

	friend FArchive& operator<<(FArchive& Ar, FBSFCompressionOptions& Options);
};

/** 트랜스폼 데이터 퀀타이즈(비트 단위로 축소) 옵션 */
USTRUCT(BlueprintType)
struct FBSFQuantizationOptions
{
	GENERATED_BODY()

	/** 위치(translation) 퀀타이즈 비트 수 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Quantization", meta=(ClampMin="8",ClampMax="32"))
	int32 PositionBits = 16;

	/** 회전(rotation) 퀀타이즈 비트 수 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Quantization", meta=(ClampMin="8",ClampMax="32"))
	int32 RotationBits = 16;

	/** 스케일(scale) 퀀타이즈 비트 수 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Quantization", meta=(ClampMin="8",ClampMax="32"))
	int32 ScaleBits = 16;

	/** 하나라도 32비트 미만이면 퀀타이즈를 실시 */
	bool IsEnabled() const;

	friend FArchive& operator<<(FArchive& Ar, FBSFQuantizationOptions& Options);
};

/** 파일 저장·로드를 제어하는 최상위 옵션 묶음 */
USTRUCT(BlueprintType)
struct FBloodStainFileOptions
{
	GENERATED_BODY()

	/** 압축 옵션 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="File|Compression")
	FBSFCompressionOptions Compression;

	/** 본 트랜스폼 퀀타이제이션 옵션 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="File|Quantization")
	FBSFQuantizationOptions Quantization;

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