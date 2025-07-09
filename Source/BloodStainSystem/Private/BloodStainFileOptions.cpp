// Fill out your copyright notice in the Description page of Project Settings.


#include "BloodStainFileOptions.h"


bool FBSFQuantizationOptions::IsEnabled() const
{
	return PositionBits < 32 || RotationBits < 32 || ScaleBits < 32;
}

FArchive& operator<<(FArchive& Ar, FBSFCompressionOptions& Options)
{
	Ar << Options.Method;
	Ar << Options.Level;
	return Ar;
}

FArchive& operator<<(FArchive& Ar, FBSFQuantizationOptions& Options)
{
	Ar << Options.PositionBits;
	Ar << Options.RotationBits;
	Ar << Options.ScaleBits;
	return Ar;
}

FArchive& operator<<(FArchive& Ar, FBloodStainFileOptions& Options)
{
	Ar << Options.Compression;
	Ar << Options.Quantization;
	Ar << Options.bIncludeChecksum;
	Ar << Options.bUseNetQuantize;
	return Ar;
}

FArchive& operator<<(FArchive& Ar, FBloodStainFileHeader& Header)
{
	Ar << Header.Magic;
	Ar << Header.Version;
	Ar << Header.Options;
	Ar << Header.UncompressedSize;
	return Ar;
}
