#include "QuantizationHelper.h"
#include "BloodStainFileUtils.h"    // FRecordSaveData 등 정의
#include "BloodStainFileOptions.h"  // FQuantizationOptions
#include "QuantizationTypes.h"

namespace BloodStainFileUtils_Internal
{

void SerializeQuantizedTransform(
    FArchive& Ar,
    const FTransform& Transform,
    const FQuantizationOptions& QuantOpts)
{
    switch (QuantOpts.Method)
    {
    case ETransformQuantizationMethod::Standard_High:
        {
            FQuantizedTransform_High Q(Transform);
            Ar << Q;  // operator<<(FArchive&, FQuantizedTransform_High&)
        }
        break;
    case ETransformQuantizationMethod::Standard_Compact:
        {
            FQuantizedTransform_Compact Q(Transform);
            Ar << Q;
        }
        break;
    case ETransformQuantizationMethod::None:
    default:
        {
            // 기본 FTransform 직렬화
            Ar << const_cast<FTransform&>(Transform);
        }
    }
}

FTransform DeserializeQuantizedTransform(
    FArchive& Ar,
    const FQuantizationOptions& QuantOpts)
{
    switch (QuantOpts.Method)
    {
    case ETransformQuantizationMethod::Standard_High:
        {
            FQuantizedTransform_High Q;
            Ar << Q;
            return Q.ToTransform();
        }
    case ETransformQuantizationMethod::Standard_Compact:
        {
            FQuantizedTransform_Compact Q;
            Ar << Q;
            return Q.ToTransform();
        }
    case ETransformQuantizationMethod::None:
    default:
        {
            FTransform T;
            Ar << T;
            return T;
        }
    }
}

void SerializeSaveData(
    FArchive& RawAr,
    FRecordSaveData& SaveData,
    FQuantizationOptions& QuantOpts)
{
    // 1) Header
    RawAr << SaveData.Header;

    // 2) Actor 배열 길이
    int32 NumActors = SaveData.RecordActorDataArray.Num();
    RawAr << NumActors;

    for (FRecordActorSaveData& ActorData : SaveData.RecordActorDataArray)
    {
        // Actor 식별자
        RawAr << ActorData.ComponentName;
        RawAr << ActorData.InitialComponentStructure;
        RawAr << ActorData.ComponentIntervals; 

        // Frame 개수
        int32 NumFrames = ActorData.RecordedFrames.Num();
        RawAr << NumFrames;

        for (FRecordFrame& Frame : ActorData.RecordedFrames)
        {
            // 타임스탬프, 추가/제거 이벤트
            RawAr << Frame.TimeStamp;
            RawAr << Frame.AddedComponents;
            RawAr << Frame.RemovedComponentNames;
            RawAr << Frame.FrameIndex;

            // ComponentTransforms
            int32 NumComps = Frame.ComponentTransforms.Num();
            RawAr << NumComps;
            for (auto& Pair : Frame.ComponentTransforms)
            {
                RawAr << Pair.Key;
                SerializeQuantizedTransform(RawAr, Pair.Value, QuantOpts);
            }

            // SkeletalMeshBoneTransforms
            int32 NumBoneMaps = Frame.SkeletalMeshBoneTransforms.Num();
            RawAr << NumBoneMaps;
            for (auto& BonePair : Frame.SkeletalMeshBoneTransforms)
            {
                RawAr << BonePair.Key;

                const FBoneComponentSpace& Space = BonePair.Value;
                int32 BoneCount = Space.BoneTransforms.Num();
                RawAr << BoneCount;
                for (const FTransform& BoneT : Space.BoneTransforms)
                {
                    SerializeQuantizedTransform(RawAr, BoneT, QuantOpts);
                }
            }
        }
    }
}

void DeserializeSaveData(
    FArchive& DataAr,
    FRecordSaveData& OutData,
    const FQuantizationOptions& QuantOpts)
{
    // 1) Header
    DataAr << OutData.Header;

    // 2) Actor 배열 길이
    int32 NumActors = 0;
    DataAr << NumActors;
    OutData.RecordActorDataArray.Empty(NumActors);

    for (int32 i = 0; i < NumActors; ++i)
    {
        FRecordActorSaveData ActorData;
        DataAr << ActorData.ComponentName;
        DataAr << ActorData.InitialComponentStructure;
        DataAr << ActorData.ComponentIntervals;  

        int32 NumFrames = 0;
        DataAr << NumFrames;
        ActorData.RecordedFrames.Empty(NumFrames);

        for (int32 f = 0; f < NumFrames; ++f)
        {
            FRecordFrame Frame;
            DataAr << Frame.TimeStamp;
            DataAr << Frame.AddedComponents;
            DataAr << Frame.RemovedComponentNames;
            DataAr << Frame.FrameIndex; 

            // ComponentTransforms
            int32 NumComps = 0;
            DataAr << NumComps;
            for (int32 c = 0; c < NumComps; ++c)
            {
                FString Key;
                DataAr << Key;
                FTransform T = DeserializeQuantizedTransform(DataAr, QuantOpts);
                Frame.ComponentTransforms.Add(Key, T);
            }

            // SkeletalMeshBoneTransforms
            int32 NumBoneMaps = 0;
            DataAr << NumBoneMaps;
            for (int32 bm = 0; bm < NumBoneMaps; ++bm)
            {
                FString Key;
                DataAr << Key;
                int32 BoneCount = 0;
                DataAr << BoneCount;
                FBoneComponentSpace Space;
                Space.BoneTransforms.Empty(BoneCount);
                for (int32 b = 0; b < BoneCount; ++b)
                {
                    FTransform BoneT = DeserializeQuantizedTransform(DataAr, QuantOpts);
                    Space.BoneTransforms.Add(BoneT);
                }
                Frame.SkeletalMeshBoneTransforms.Add(Key, Space);
            }

            ActorData.RecordedFrames.Add(Frame);
        }

        OutData.RecordActorDataArray.Add(ActorData);
    }
}

} // namespace BloodStainFileUtils_Internal
