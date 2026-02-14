// Copyright © 2026 Sanjyot Dahale.


#include "LiteRTLMFunctionLib.h"
#include "Logging/LogMacros.h"
#include "Async/Async.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"

FLiteRTToolExecutorDelegate ULiteRTLMFunctionLib::GToolExecutorDelegate;
FSttResponseDelegate ULiteRTLMFunctionLib::GSttResponseDelegate;

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
            UE_LOG(LogTemp, Log, TEXT("LiteRT-STT RecognizedText : %s"), *RecognizedText);

            if (ULiteRTLMFunctionLib::GSttResponseDelegate.IsBound())
            {
                ULiteRTLMFunctionLib::GSttResponseDelegate.Execute(RecognizedText);
            }
            else
            {
                UE_LOG(LogTemp, Warning, TEXT("LiteRT-STT: No SttResponseHandler Registered!"));
            }
        });
}


JNIEXPORT jstring JNICALL Java_com_epicgames_unreal_GameActivity_nativeOnToolExecution(JNIEnv* jenv, jobject thiz, jstring jFunctionName, jstring jParams)
{
    FString FunctionName = FJavaHelper::FStringFromParam(jenv, jFunctionName);
    FString Params = FJavaHelper::FStringFromParam(jenv, jParams);

    UE_LOG(LogTemp, Log, TEXT("LiteRT-Tool: Requested Execution: %s with Params: %s"), *FunctionName, *Params);

    // We need to execute this on the GameThread, but we must return a value synchronously to Java.
    // So we will pause this thread (which checks out as being a background thread from Java) until the GT finishes.

    FString ToolResult = TEXT("{}");
    FEvent* SyncEvent = FPlatformProcess::GetSynchEventFromPool(false);

    if (SyncEvent)
    {
        AsyncTask(ENamedThreads::GameThread, [FunctionName, Params, &ToolResult, SyncEvent]()
            {
                if (ULiteRTLMFunctionLib::GToolExecutorDelegate.IsBound())
                {
                    ToolResult = ULiteRTLMFunctionLib::GToolExecutorDelegate.Execute(FunctionName, Params);
                }
                else
                {
                    UE_LOG(LogTemp, Warning, TEXT("LiteRT-Tool: No ToolExecutor registered!"));
                    ToolResult = TEXT("{\"error\": \"No tool executor registered in Unreal\"}");
                }
                SyncEvent->Trigger();
            });

        // Wait for the GameThread to finish (with a timeout just in case)
        SyncEvent->Wait(10000); // 10 seconds timeout
        FPlatformProcess::ReturnSynchEventToPool(SyncEvent);
    }

    jstring jResult = jenv->NewStringUTF(TCHAR_TO_UTF8(*ToolResult));
    return jResult;
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

void ULiteRTLMFunctionLib::SubmitToolResult(FString FunctionName, FString JsonResults, FLiteRTToolResultResponseDelegate OnComplete)
{
    AsyncTask(ENamedThreads::AnyBackgroundHiPriTask, [FunctionName, JsonResults, OnComplete]()
        {
            FString Result = TEXT("Error during tool result response: AI Failed to respond");
#if PLATFORM_ANDROID
            if (JNIEnv* Env = FAndroidApplication::GetJavaEnv())
            {
                static jmethodID ToolMethod = FJavaWrapper::FindMethod(Env,
                    FJavaWrapper::GameActivityClassID, "AndroidThunkJava_SubmitFunctionResult",
                    "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;", false);

                jstring jName = Env->NewStringUTF(TCHAR_TO_UTF8(*FunctionName));
                jstring jRes = Env->NewStringUTF(TCHAR_TO_UTF8(*JsonResults));
                jstring jResult = (jstring)FJavaWrapper::CallObjectMethod(Env, FJavaWrapper::GameActivityThis, ToolMethod, jName, jRes);

                // Convert Java String back to Unreal FString
                const char* ResultChars = Env->GetStringUTFChars(jResult, 0);
                Result = FString(UTF8_TO_TCHAR(ResultChars));

                // Clean up refs...
                Env->ReleaseStringUTFChars(jResult, ResultChars);
                Env->DeleteLocalRef(jName);
                Env->DeleteLocalRef(jRes);
            }
            UE_LOG(LogTemp, Warning, TEXT("BP_Gemma: SubmitToolResult Response: %s"), *Result);
#else
            FPlatformProcess::Sleep(1.5f);
            Result = TEXT("Editor Simulation: This is a response to: ") + FunctionName + JsonResults;
#endif
            AsyncTask(ENamedThreads::GameThread, [Result, OnComplete]() 
                {
                    OnComplete.ExecuteIfBound(Result);
                });
        });

}

void ULiteRTLMFunctionLib::RegisterToolExecutor(FLiteRTToolExecutorDelegate Executor)
{
    GToolExecutorDelegate = Executor;
    UE_LOG(LogTemp, Log, TEXT("LiteRT-LM: Tool Executor Registered"));
}

void ULiteRTLMFunctionLib::HandleSttResponse(FSttResponseDelegate ResponseHandler)
{
    GSttResponseDelegate = ResponseHandler;
    UE_LOG(LogTemp, Log, TEXT("LiteRT-LM: STT Response Handler Registered"));
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

bool ULiteRTLMFunctionLib::ParseFunctionCall(FString RawResponse, FString& OutFunctionName, TMap<FString, FString>& OutParameters)
{
    // 1. Clean the outer tags
    // This removes <start_function_call>call: and <end_function_call> regardless of their exact index
    FString WorkingString = RawResponse;
    WorkingString = WorkingString.Replace(TEXT("<start_function_call>call:"), TEXT(""));
    WorkingString = WorkingString.Replace(TEXT("<end_function_call>"), TEXT(""));

    UE_LOG(LogTemp, Log, TEXT("Gemma-Parser: Cleaned String: %s"), *WorkingString);

    // 2. Identify the Function Name and Parameter Block
    // Logic: Split at the first '{'
    FString ParamBlock;
    if (!WorkingString.Split(TEXT("{"), &OutFunctionName, &ParamBlock))
    {
        // If there's no '{', it might be a function with no parameters
        OutFunctionName = WorkingString.TrimStartAndEnd();
        return !OutFunctionName.IsEmpty();
    }

    // Remove the trailing '}' from the parameter block
    ParamBlock = ParamBlock.Replace(TEXT("}"), TEXT(""));
    ParamBlock = ParamBlock.TrimStartAndEnd();
    if (!ParamBlock.IsEmpty()) {

        // 3. Clean the Parameter Block
        // Remove all <escape> tags before parsing keys/values
        ParamBlock = ParamBlock.Replace(TEXT("<escape>"), TEXT(""));

        UE_LOG(LogTemp, Log, TEXT("Gemma-Parser: Params to parse: %s"), *ParamBlock);

        // 4. Manually Parse Key-Value Pairs
        // Logic: Split by ',' (for multiple params) and then by ':'
        TArray<FString> PairArray;
        ParamBlock.ParseIntoArray(PairArray, TEXT(","), true);

        for (const FString& FullPair : PairArray)
        {
            FString Key, Value;
            if (FullPair.Split(TEXT(":"), &Key, &Value))
            {
                OutParameters.Add(Key.TrimStartAndEnd(), Value.TrimStartAndEnd());
                UE_LOG(LogTemp, Log, TEXT("Gemma-Parser: Found Param -> %s : %s"), *Key, *Value);
            }
        }
    }
    return !OutFunctionName.IsEmpty();
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
    if (JNIEnv* Env = FAndroidApplication::GetJavaEnv())
    {
        static jmethodID InitMethod = FJavaWrapper::FindMethod(Env, FJavaWrapper::GameActivityClassID, "AndroidThunkJava_InitSTT", "()V", false);
        FJavaWrapper::CallVoidMethod(Env, FJavaWrapper::GameActivityThis, InitMethod);
    }
#endif
}

void ULiteRTLMFunctionLib::StartSTT() {
#if PLATFORM_ANDROID
    if (JNIEnv* Env = FAndroidApplication::GetJavaEnv()) {
        static jmethodID Method = FJavaWrapper::FindMethod(Env, FJavaWrapper::GameActivityClassID, "AndroidThunkJava_StartListening", "()V", false);
        FJavaWrapper::CallVoidMethod(Env, FJavaWrapper::GameActivityThis, Method);
    }
#endif
}

void ULiteRTLMFunctionLib::StopSTT() {
#if PLATFORM_ANDROID
    if (JNIEnv* Env = FAndroidApplication::GetJavaEnv()) {
        static jmethodID Method = FJavaWrapper::FindMethod(Env, FJavaWrapper::GameActivityClassID, "AndroidThunkJava_StopListening", "()V", false);
        FJavaWrapper::CallVoidMethod(Env, FJavaWrapper::GameActivityThis, Method);
    }
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


