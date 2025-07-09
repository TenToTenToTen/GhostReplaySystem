#include "BloodStainCompressionUtils.h"
#include "Misc/Compression.h"

namespace BloodStainCompressionUtils
{

    static FName CompressionFormat(EBSFCompressionMethod Method)
    {
        switch (Method)
        {
        case EBSFCompressionMethod::Zlib: return NAME_Zlib;
        case EBSFCompressionMethod::Gzip: return NAME_Gzip;
        case EBSFCompressionMethod::LZ4:  return NAME_LZ4;
        default:                          return NAME_None;
        }
    }

    bool CompressBuffer(
        const TArray<uint8>& InBuffer,
        TArray<uint8>&       OutCompressed,
        const FBSFCompressionOptions& Opts)
    {
        if (Opts.Method == EBSFCompressionMethod::None)
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

    bool DecompressBuffer(
        const TArray<uint8>& Compressed,
        TArray<uint8>&       OutRaw,
        const FBSFCompressionOptions& Opts,
        int64                UncompressedSize)
    {
        if (Opts.Method == EBSFCompressionMethod::None)
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

} // namespace BloodStainCompressionUtils
