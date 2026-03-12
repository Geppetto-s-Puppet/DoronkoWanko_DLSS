using BepInEx;
using BepInEx.Logging;
using HarmonyLib;
using System;
using System.Runtime.InteropServices;
using UnityEngine;
using UnityEngine.Rendering;
using UnityEngine.Rendering.Universal;
using UnityEngine.SceneManagement;

// ─────────────────────────────────────────────────────────────────────

namespace DoronkoWanko_DLSS
{
    [BepInPlugin("com.alex.doronkowankodlss", "DoronkoWanko DLSS", "2.0.3")]
    public class Plugin : BaseUnityPlugin
    {
        internal static ManualLogSource Log;
        void Awake()
        {
            SceneManager.sceneLoaded += OnSceneLoaded;
            new Harmony("com.alex.doronkowankodlss").PatchAll();
            (Log = Logger).LogInfo("Plugin loaded successfully!");
        }

        internal static DW_DLSS dlss;
        void OnSceneLoaded(Scene scene, LoadSceneMode mode) // ゲームのクリーンアップ後に発火させる
        {
            if (dlss == null) { DontDestroyOnLoad(dlss = new GameObject("DW_DLSS").AddComponent<DW_DLSS>()); }

            foreach (var cam in Camera.allCameras)
            {
                if (cam.name == "Main Camera") dlss.mainCamera = cam;
                else if (cam.name == "UI Camera") dlss.uiCamera = cam;
                Log.LogInfo($"Scanned: {scene.name}/{cam.name}({cam.depth})" +
                    $", clearFlags: {cam.clearFlags}" +
                    $",cullingMask: {cam.cullingMask}" +
                    $", targetTexture: {cam.targetTexture}" +
                    $", renderPath: {cam.actualRenderingPath}" +
                    $", pipeline: {UnityEngine.Rendering.GraphicsSettings.currentRenderPipeline.GetType()}");
            }
        }

        // ─────────────────────────────────────────────────────────────────────

        internal class DW_DLSS : MonoBehaviour
        {

            internal Camera uiCamera, mainCamera; // UIにはシェーダーを適用しない
            private static string shaderPath; // フォルダのパスだけを渡してC++側で読込
            private static bool uKeyWasDown, rKeyWasDown; // UIトグル(U)とシェーダー読込(R)
            internal RenderTexture[] rts = new RenderTexture[8]; // シェーダーに渡すテクスチャ

            [DllImport("user32.dll")] static extern short GetAsyncKeyState(int vKey);
            void Update() { if (!Application.isFocused) return; HandleHotkeyInputs(); }

            //[DllImport("DW_DLSS_N.dll")] static extern void DW_Release();
            //void OnDestroy() => DW_Release(); // 終了して最後に呼び出される関数
            //[DllImport("DW_DLSS_N.dll")] static extern void DW_Init();
            //void Start() => DW_Init(); // ダミーテクスチャを渡してデバイス同期させたい

            // ─────────────────────────────────────────────────────────────────────

            void HandleHotkeyInputs()
            {
                bool uDown = (GetAsyncKeyState(0x55) & 0x8000) != 0;
                if (uDown && !uKeyWasDown && uiCamera != null) uiCamera.enabled ^= true;
                uKeyWasDown = uDown;

                bool rDown = (GetAsyncKeyState(0x52) & 0x8000) != 0;
                if (rDown && !rKeyWasDown) { shaderPath = null; BrowseShaderFolderAsync(); }
                rKeyWasDown = rDown;
            }

            private static readonly string pluginDir = System.IO.Path.Combine(
                AppDomain.CurrentDomain.BaseDirectory, "BepInEx", "plugins");
            [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
            struct OPENFILENAME
            {
                public int lStructSize;
                public IntPtr hwndOwner, hInstance;
                public string lpstrFilter, lpstrCustomFilter;
                public int nMaxCustFilter, nFilterIndex;
                public string lpstrFile;
                public int nMaxFile;
                public string lpstrFileTitle;
                public int nMaxFileTitle;
                public string lpstrInitialDir, lpstrTitle;
                public int Flags;
                public short nFileOffset, nFileExtension;
                public string lpstrDefExt;
                public IntPtr lCustData, lpfnHook;
                public string lpTemplateName;
            }
            [DllImport("comdlg32.dll", CharSet = CharSet.Unicode)]
            static extern bool GetOpenFileName(ref OPENFILENAME ofn);
            void BrowseShaderFolderAsync()
            {
                var thread = new System.Threading.Thread(() =>
                {
                    var ofn = new OPENFILENAME
                    {
                        lStructSize = Marshal.SizeOf<OPENFILENAME>(),
                        lpstrFile = new string('\0', 256),
                        nMaxFile = 256,
                        lpstrFilter = "HLSL Files\0*.hlsl\0",
                        nFilterIndex = 1,
                        lpstrInitialDir = pluginDir,
                        lpstrTitle = "Choose the shader",
                        Flags = 0x00001000,
                    };
                    if (GetOpenFileName(ref ofn)) { shaderPath = ofn.lpstrFile; Log.LogInfo($"chose: {shaderPath}"); }
                });
                thread.SetApartmentState(System.Threading.ApartmentState.STA);
                thread.IsBackground = true;
                thread.Start();
            }
        }

        //// shaderPath=nullのときは何も読まない、visible=falseのときは非表示
        //[DllImport("DW_DLSS_N.dll", CharSet = CharSet.Unicode)]
        //static extern void DW_Update(string shaderPath, IntPtr[] textures, [MarshalAs(UnmanagedType.Bool)] bool visible, int w, int h);

        //[DllImport("DW_DLSS_N.dll")]
        //static extern void DW_Draw();

        //[DllImport("DW_DLSS_N.dll")]
        //static extern void DW_Release();

        //[DllImport("comdlg32.dll", CharSet = CharSet.Unicode)]
        //static extern bool GetOpenFileName(ref OPENFILENAME ofn);


        //    };

        //    StartCoroutine(EndOfFrameLoop());
        //    StartCoroutine(TryAddFeatureDelayed());
        //}

        //System.Collections.IEnumerator InitAndCleanup()
        //{
        //    // UnityのD3D12Resourceポインタを渡してデバイスを共有させる
        //    var probe = new RenderTexture(4, 4, 0, RenderTextureFormat.ARGB32);
        //    probe.Create();
        //    DW_Init(probe.GetNativeTexturePtr());
        //    yield return null;
        //    probe.Release();
        //    Destroy(probe);
        //}

    }

    // ─────────────────────────────────────────────────────────────────────

    [HarmonyPatch(typeof(ScriptableRenderer), "Execute")]
    class ScriptableRenderer_Execute_Patch
    {
        static readonly string[] rtFields = {
            "m_ActiveCameraColorAttachment",   // rts[0] 最終出力候補
            // ここから下は全部nullった
            "m_ActiveCameraDepthAttachment",   // rts[1] 深度バッファ
            "m_CameraDepthAttachment",         // rts[2] 深度バッファ2
            "m_DepthTexture",                  // rts[3] 深度テクスチャ
            "m_OpaqueColor",                   // rts[4] Opaque
            "_CameraDepthTexture",             // rts[5] 深度(Global)
            "_CameraDepthNormalsTexture",      // rts[6] 法線+深度
            "_CameraMotionVectorsTexture",     // rts[7] モーションベクター
            };

        static void Postfix(ScriptableRenderContext context, ref RenderingData renderingData)
        {
            var cam = renderingData.cameraData.camera;
            if (cam.name != "Main Camera") return;

            var renderer = renderingData.cameraData.renderer;
            var flags = System.Reflection.BindingFlags.NonPublic | System.Reflection.BindingFlags.Instance;

            for (int i = 0; i < rtFields.Length; i++)
            {
                var name = rtFields[i];
                RenderTexture rt = null;

                if (name.StartsWith("_"))
                {
                    // ★ _始まりはShaderGlobal
                    rt = Shader.GetGlobalTexture(name) as RenderTexture;
                }
                else
                {
                    // ★ それ以外はReflectionでRenderTargetHandleから取る
                    var handle = renderer.GetType().GetField(name, flags)?.GetValue(renderer);
                    if (handle != null)
                    {
                        var id = (int)handle.GetType().GetProperty("id")?.GetValue(handle);
                        rt = Shader.GetGlobalTexture(id) as RenderTexture;
                    }
                }

                Plugin.dlss.rts[i] = rt;
                Plugin.Log.LogInfo(rt != null
                    ? $"rts[{i}] {name,-40} = {rt.width}x{rt.height} {rt.graphicsFormat}"
                    : $"rts[{i}] {name,-40} = null");
            }
        }
    }




}






//opaque、シャドウ、最終出力、ライティング前、シャドウマップ、法線、深度、モーションベクター、解像度、カメラ位置、
//static void Postfix(ScriptableRenderContext context, ref RenderingData renderingData)
//{
//    var cam = renderingData.cameraData.camera;
//    if (cam.name != "Main Camera") return;

//    // ★ UniversalRendererの内部フィールドをReflectionで全列挙。何が取れるか確認するため
//    var renderer = renderingData.cameraData.renderer;
//    var flags = System.Reflection.BindingFlags.NonPublic | System.Reflection.BindingFlags.Instance;
//    foreach (var field in renderer.GetType().GetFields(flags))
//    {
//        if (field.Name.ToLower().Contains("color") ||
//            field.Name.ToLower().Contains("depth") ||
//            field.Name.ToLower().Contains("attach") ||
//            field.Name.ToLower().Contains("target"))
//        {
//            var val = field.GetValue(renderer);
//            Plugin.Log.LogInfo($"[Field] {field.Name} = {val?.GetType().Name} : {val}");
//        }
//    }
//    Plugin.Log.LogInfo($" --------------------------------------------------------- ");
//}