using BepInEx;
using BepInEx.Logging;
using System;
using System.Runtime.InteropServices;
using UnityEngine;
using UnityEngine.SceneManagement;

// ─────────────────────────────────────────────────────────────────────

namespace DoronkoWanko_DLSS
{
    [BepInPlugin("com.alex.doronkowankodlss", "DoronkoWanko DLSS", "2.0.2")]
    public class Plugin : BaseUnityPlugin
    {
        internal static ManualLogSource Log;
        void Awake()
        {
            SceneManager.sceneLoaded += OnSceneLoaded;
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
                Log.LogInfo($"Scanned: {scene.name}/{cam.name}({cam.depth})");
            }
        }

        // ─────────────────────────────────────────────────────────────────────

        internal class DW_DLSS : MonoBehaviour
        {

            internal Camera uiCamera, mainCamera; // UIにはシェーダーを適用しない
            private static string shaderPath; // フォルダのパスだけを渡してC++側で読込
            private static bool uKeyWasDown, rKeyWasDown; // UIトグル(U)とシェーダー読込(R)

            [DllImport("user32.dll")] static extern short GetAsyncKeyState(int vKey);
            void Update() { if (!Application.isFocused) return; HandleHotkeyInputs(); }

            // デバッグ用
            void OnDisable() => Plugin.Log.LogInfo("ありえないからねこれありえないからねこれありえないからねこれありえないからねこれ");
            void OnDestroy() => Plugin.Log.LogInfo("ありえないからねこれありえないからねこれありえないからねこれありえないからねこれ");

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

            // ─────────────────────────────────────────────────────────────────────

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

        //[DllImport("DW_DLSS_N.dll")]
        //static extern void DW_Init(IntPtr sampleResourcePtr);

        //// shaderPath=nullのときは何も読まない、visible=falseのときは非表示
        //[DllImport("DW_DLSS_N.dll", CharSet = CharSet.Unicode)]
        //static extern void DW_Update(string shaderPath, IntPtr[] textures, [MarshalAs(UnmanagedType.Bool)] bool visible, int w, int h);

        //[DllImport("DW_DLSS_N.dll")]
        //static extern void DW_Draw();

        //[DllImport("DW_DLSS_N.dll")]
        //static extern void DW_Release();

        //[DllImport("comdlg32.dll", CharSet = CharSet.Unicode)]
        //static extern bool GetOpenFileName(ref OPENFILENAME ofn);

        //[DllImport("user32.dll")]
        //static extern short GetAsyncKeyState(int vKey);

        //[StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
        //struct OPENFILENAME
        //{
        //    public int lStructSize;
        //    public IntPtr hwndOwner, hInstance;
        //    public string lpstrFilter, lpstrCustomFilter;
        //    public int nMaxCustFilter, nFilterIndex;
        //    public string lpstrFile;
        //    public int nMaxFile;
        //    public string lpstrFileTitle;
        //    public int nMaxFileTitle;
        //    public string lpstrInitialDir, lpstrTitle;
        //    public int Flags;
        //    public short nFileOffset, nFileExtension;
        //    public string lpstrDefExt;
        //    public IntPtr lCustData, lpfnHook;
        //    public string lpTemplateName;
        //}

        //void Awake()
        //{
        //    StartCoroutine(InitAndCleanup());

        //    CapturePass.OnCapture = (colorPtr, depthPtr, normalPtr, motionPtr, shadowPtr, shadowAddPtr, opaquePtr, ssaoPtr, w, h) =>
        //    {
        //        //var ptrs = new IntPtr[8] { colorPtr, depthPtr, normalPtr, motionPtr, shadowPtr, shadowAddPtr, opaquePtr, ssaoPtr };
        //        bool visible = Application.isFocused && shaderPath != null;
        //        //DW_Update(shaderPath, new IntPtr[8], visible, w, h);
        //        //DW_Update(shaderPath, ptrs, visible, w, h);

        //        var ptrs = new IntPtr[8];
        //        ptrs[0] = colorPtr;
        //        ptrs[1] = depthPtr;
        //        ptrs[2] = normalPtr;
        //        //ptrs[3] = motionPtr;
        //        DW_Update(shaderPath, ptrs, visible, w, h);


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




        //void OnDestroy() => DW_Release();


    }



    // ────────────────────────────────────────────────────────────────────

}