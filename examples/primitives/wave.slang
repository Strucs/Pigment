module wave;

import buffer;

using pigment.std;

struct PushConstants {
    Buffer<float, Access.ReadWrite> heights;
    float time;
    uint count;
};

[shader("compute")]
[numthreads(64, 1, 1)]
void computeMain(uint3 thread_id: SV_DispatchThreadID, uniform PushConstants push)
{
    uint index = thread_id.x;
    if(index >= push.count)
    {
        return;
    }

    float phase         = float(index % 10);
    push.heights[index] = sin(push.time * 2.0 + phase * 0.3) * 0.8;
}
