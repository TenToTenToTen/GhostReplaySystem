/*
* Copyright 2025 TenToTen, All Rights Reserved.
*/


#include "BloodStainCompressionUtils.h"
#include "Misc/Compression.h"

namespace BloodStainCompressionUtils
{
    static FName CompressionFormat(ECompressionMethod Method)
    {
        switch (Method)
        {
        case ECompressionMethod::Zlib: return NAME_Zlib;
        case ECompressionMethod::Gzip: return NAME_Gzip;
        case ECompressionMethod::LZ4:  return NAME_LZ4;
        default:                          return NAME_None;
        }
    }

    bool BloodStainCompressionUtils::CompressBuffer(const TArray<uint8>& InBuffer, TArray<uint8>& OutCompressed, FCompressionOption Opts)
    {
        if (Opts.Method == ECompressionMethod::None)
        {
            OutCompressed = InBuffer;
            return true;
        }

        FName Format = CompressionFormat(Opts.Method);
        int32 MaxSize = FCompression::CompressMemoryBound(Format, InBuffer.Num());
        OutCompressed.SetNumUninitialized(MaxSize);

        int64 CompressedSize = MaxSize;
        if (!FCompression::CompressMemory(
            Format,
            OutCompressed.GetData(), CompressedSize,
            InBuffer.GetData(), InBuffer.Num(),
            COMPRESS_NoFlags))
        {
            return false;
        }

        OutCompressed.SetNum(int32(CompressedSize));
        return true;
    }

    bool BloodStainCompressionUtils::DecompressBuffer(const TArray<uint8>& Compressed, TArray<uint8>& OutRaw, int64 UncompressedSize, FCompressionOption Opts)
    {
        if (Opts.Method == ECompressionMethod::None)
        {
            OutRaw = Compressed;
            return true;
        }

        OutRaw.SetNumUninitialized(UncompressedSize);
        return FCompression::UncompressMemory(
            CompressionFormat(Opts.Method),
            OutRaw.GetData(), UncompressedSize,
            Compressed.GetData(), Compressed.Num()
        );
    }

}
