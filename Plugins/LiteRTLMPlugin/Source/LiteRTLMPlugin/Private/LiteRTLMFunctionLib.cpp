// Fill out your copyright notice in the Description page of Project Settings.


#include "LiteRTLMFunctionLib.h"
#include "Logging/LogMacros.h"
#include "Async/Async.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"

#if PLATFORM_ANDROID
#include "Android/AndroidJava.h"
#include "Android/AndroidJNI.h"
#include "Android/AndroidApplication.h"

JNIEXPORT void JNICALL Java_com_epicgames_unreal_GameActivity_nativeOnSTTResult(JNIEnv* jenv, jobject thiz, jstring jtext)
{
    FString RecognizedText = FJavaHelper::FStringFromParam(jenv, jtext);

    // Use AsyncTask to ensure we return to the GameThread before calling Unreal functions
    AsyncTask(ENamedThreads::GameThread, [RecognizedText]()
        {
            UE_LOG(LogTemp, Log, TEXT("LiteRT-STT: %s"), *RecognizedText);

            // Pass the speech directly to your existing Gemma generation function
            // ULiteRTLMFunctionLibrary::GenerateLMResponseAsync(RecognizedText);
        });
}

#endif


void ULiteRTLMFunctionLib::InitializeLM(FString ModelPath)
{
#if PLATFORM_ANDROID
    if (JNIEnv* Env = FAndroidApplication::GetJavaEnv())
    {
        static jmethodID InitMethod = FJavaWrapper::FindMethod(Env,
            FJavaWrapper::GameActivityClassID,
            "AndroidThunkJava_InitWithAssetName",
            "(Ljava/lang/String;)V",
            false);

        jstring jName = Env->NewStringUTF(TCHAR_TO_UTF8(*ModelPath));
        FJavaWrapper::CallVoidMethod(Env, FJavaWrapper::GameActivityThis, InitMethod, jName);
        Env->DeleteLocalRef(jName);
    }
#else
    UE_LOG(LogTemp, Warning, TEXT("LiteRT-LM: Logic only runs on Android hardware."));
#endif
}

void ULiteRTLMFunctionLib::GenerateLMResponseAsync(FString Prompt, FLiteRTResponseDelegate OnComplete)
{
    // 1. Move the task to a background thread
    AsyncTask(ENamedThreads::AnyBackgroundHiPriTask, [Prompt, OnComplete]()
        {
            FString Result = TEXT("Error: AI Failed to respond");

#if PLATFORM_ANDROID
            if (JNIEnv* Env = FAndroidApplication::GetJavaEnv())
            {
                // Find the Java Method
                static jmethodID GenMethod = FJavaWrapper::FindMethod(Env,
                    FJavaWrapper::GameActivityClassID,
                    "AndroidThunkJava_GenerateResponse",
                    "(Ljava/lang/String;)Ljava/lang/String;",
                    false);

                jstring jPrompt = Env->NewStringUTF(TCHAR_TO_UTF8(*Prompt));

                // Call the JVM (This is the slow part)
                jstring jResult = (jstring)FJavaWrapper::CallObjectMethod(Env, FJavaWrapper::GameActivityThis, GenMethod, jPrompt);

                // Convert Java String back to Unreal FString
                const char* ResultChars = Env->GetStringUTFChars(jResult, 0);
                Result = FString(UTF8_TO_TCHAR(ResultChars));

                // Clean up JNI refs
                Env->ReleaseStringUTFChars(jResult, ResultChars);
                Env->DeleteLocalRef(jPrompt);
                Env->DeleteLocalRef(jResult);
            }
            UE_LOG(LogTemp, Warning, TEXT("BP_Gemma: GenerateResponse Result: %s"), *Result);
#else
            // Simulation for testing in Editor
            FPlatformProcess::Sleep(1.5f);
            Result = TEXT("Editor Simulation: This is a response to: ") + Prompt;
#endif

            // 2. Switch back to the Game Thread to safely send the result back to Blueprints/UI
            AsyncTask(ENamedThreads::GameThread, [Result, OnComplete]()
                {
                    // Use ExecuteIfBound for standard delegates
                    OnComplete.ExecuteIfBound(Result);
                });
        });
}

void ULiteRTLMFunctionLib::SubmitToolResult(FString FunctionName, FString JsonResults)
{
#if PLATFORM_ANDROID
    if (JNIEnv* Env = FAndroidApplication::GetJavaEnv())
    {
        static jmethodID ToolMethod = FJavaWrapper::FindMethod(Env,
            FJavaWrapper::GameActivityClassID, "AndroidThunkJava_SubmitFunctionResult",
            "(Ljava/lang/String;Ljava/lang/String;)V", false);

        jstring jName = Env->NewStringUTF(TCHAR_TO_UTF8(*FunctionName));
        jstring jRes = Env->NewStringUTF(TCHAR_TO_UTF8(*JsonResults));
        FJavaWrapper::CallVoidMethod(Env, FJavaWrapper::GameActivityThis, ToolMethod, jName, jRes);
        
        // Clean up refs...
        Env->DeleteLocalRef(jName);
        Env->DeleteLocalRef(jRes);
    }
#endif
}

void ULiteRTLMFunctionLib::ResetConversation()
{
#if PLATFORM_ANDROID
    if (JNIEnv* Env = FAndroidApplication::GetJavaEnv())
    {
        static jmethodID ResetMethod = FJavaWrapper::FindMethod(Env,
            FJavaWrapper::GameActivityClassID, "AndroidThunkJava_ResetConversation", "()V", false);
        FJavaWrapper::CallVoidMethod(Env, FJavaWrapper::GameActivityThis, ResetMethod);
    }
#endif
}

bool ULiteRTLMFunctionLib::ParseFunctionCall(FString RawResponse, FString PrefixToChop, FString& OutFunctionName, TMap<FString, FString>& OutParameters)
{
    // 1. Check for the FunctionGemma prefix
    if (!RawResponse.StartsWith(PrefixToChop)) return false;

    // 2. Split "call:func_name{...}" into "func_name" and "{...}"
    FString CleanString = RawResponse.RightChop(26); // Remove "<start_function_call>call:"

    UE_LOG(LogTemp, Warning, TEXT("BP_Gemma: Parsing: %s"), *CleanString);

    int32 JsonStartIndex;
    int32 JsonEndIndex;
    if (!CleanString.FindChar('{', JsonStartIndex)) return false;
    if (!CleanString.FindChar('}', JsonEndIndex)) return false;

    OutFunctionName = CleanString.Left(JsonStartIndex).TrimStartAndEnd();

    if (!OutFunctionName.IsEmpty())
    {
        UE_LOG(LogTemp, Warning, TEXT("BP_Gemma: OutFunctionName: %s"), *OutFunctionName);
        // return true;
    }

    FString JsonPart = CleanString.RightChop(JsonStartIndex);
    JsonPart = JsonPart.LeftChop(19);   // Remove "<end_function_call>"

    UE_LOG(LogTemp, Warning, TEXT("BP_Gemma: Chopped JSON block: %s"), *JsonPart);

    // 3. Parse the JSON parameters
    TSharedPtr<FJsonObject> JsonObject;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonPart);

    if (FJsonSerializer::Deserialize(Reader, JsonObject) && JsonObject.IsValid())
    {
        for (auto& Pair : JsonObject->Values)
        {
            OutParameters.Add(Pair.Key, Pair.Value->AsString());
        }
        return true;
    }
    else {
        UE_LOG(LogTemp, Warning, TEXT("BP_Gemma: No Parameters found!"));
        return true;
    }

    return false;
}

void ULiteRTLMFunctionLib::ShutdownLM()
{
#if PLATFORM_ANDROID
    if (JNIEnv* Env = FAndroidApplication::GetJavaEnv())
    {
        static jmethodID CloseMethod = FJavaWrapper::FindMethod(Env,
            FJavaWrapper::GameActivityClassID, "AndroidThunkJava_ShutdownAll", "()V", false);
        FJavaWrapper::CallVoidMethod(Env, FJavaWrapper::GameActivityThis, CloseMethod);
    }
#endif
}

void ULiteRTLMFunctionLib::InitSTT()
{
#if PLATFORM_ANDROID
    /*if (JNIEnv* Env = FAndroidApplication::GetJavaEnv())
    {
        static jmethodID InitMethod = FJavaWrapper::FindMethod(Env, FJavaWrapper::GameActivityClassID, "AndroidThunkJava_InitSTT", "()V", false);
        FJavaWrapper::CallVoidMethod(Env, FJavaWrapper::GameActivityThis, InitMethod);
    }*/
#endif
}

void ULiteRTLMFunctionLib::StartSTT() {
#if PLATFORM_ANDROID
    /*if (JNIEnv* Env = FAndroidApplication::GetJavaEnv()) {
        static jmethodID Method = FJavaWrapper::FindMethod(Env, FJavaWrapper::GameActivityClassID, "AndroidThunkJava_StartListening", "()V", false);
        FJavaWrapper::CallVoidMethod(Env, FJavaWrapper::GameActivityThis, Method);
    }*/
#endif
}

void ULiteRTLMFunctionLib::StopSTT() {
#if PLATFORM_ANDROID
    /*if (JNIEnv* Env = FAndroidApplication::GetJavaEnv()) {
        static jmethodID Method = FJavaWrapper::FindMethod(Env, FJavaWrapper::GameActivityClassID, "AndroidThunkJava_StopListening", "()V", false);
        FJavaWrapper::CallVoidMethod(Env, FJavaWrapper::GameActivityThis, Method);
    }*/
#endif
}


void ULiteRTLMFunctionLib::ShutdownAIServices() {
#if PLATFORM_ANDROID
    if (JNIEnv* Env = FAndroidApplication::GetJavaEnv()) {
        static jmethodID Method = FJavaWrapper::FindMethod(Env, FJavaWrapper::GameActivityClassID, "AndroidThunkJava_ShutdownAll", "()V", false);
        FJavaWrapper::CallVoidMethod(Env, FJavaWrapper::GameActivityThis, Method);
    }
#endif
}


