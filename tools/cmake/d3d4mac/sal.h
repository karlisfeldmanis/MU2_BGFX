// Microsoft's source annotations, as nothing: the DirectX headers' WSL adapter includes <sal.h>,
// which bgfx's copy of them does not carry. Only shaderc's Mac build reads this
// (CMakeLists.txt, "Direct3D 11's shaders").
#pragma once

#define _In_
#define _In_opt_
#define _Inout_
#define _Out_
#define _Outptr_
#define _Outptr_result_maybenull_
#define _Outptr_result_nullonfailure_
#define _COM_Outptr_
#define _Success_(expr)
#define _Analysis_assume_(expr)
