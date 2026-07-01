// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class AvatarZg : ModuleRules
{
    public AvatarZg(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        // Necessario per intercettare in modo sicuro le eccezioni cv::Exception.
        bEnableExceptions = true;

        PublicDependencyModuleNames.AddRange(new string[]
        {
            // Unreal core
            "Core",
            "CoreUObject",
            "Engine",
            "InputCore",

            // Networking e JSON
            "HTTP",
            "Json",
            "JsonUtilities",

            // UI
            "UMG",

            // Plugin audio e speech recognition
            "RuntimeAudioImporter",
            "RuntimeSpeechRecognizer"
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            // UI interna
            "Slate",
            "SlateCore",
            "ApplicationCore",

            // Sistema audio Unreal
            "AudioMixer",
            "AudioCaptureCore",
            "AudioExtensions",
            "AudioPlatformConfiguration",
            "SignalProcessing",

            // Rendering e risorse GPU
            "RenderCore",
            "RHI",

            // OpenCV integrato in Unreal Engine
            "OpenCV",
            "OpenCVHelper"
        });

        if (Target.bBuildEditor)
        {
            PrivateDependencyModuleNames.AddRange(new string[]
            {
                "UnrealEd",
                "PropertyEditor",
                "EditorStyle",
                "ToolMenus"
            });
        }
    }
}
