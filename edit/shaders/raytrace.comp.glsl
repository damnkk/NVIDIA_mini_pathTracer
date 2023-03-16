#version 460
#extension GL_EXT_debug_printf : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_ray_query: require

layout(local_size_x = 16, local_size_y = 8, local_size_z = 1) in;
layout(binding = 0,set = 0, scalar) buffer storageBuffer{
   vec3 imageData[];
};
layout(binding = 1, set = 0) uniform accelerationStructureEXT tlas;
layout(binding = 2, set = 0, scalar) buffer Vertices{
   vec3 vertices[];
};
layout(binding = 3, set = 0, scalar) buffer Indices{
   uint indices[];
};



const uvec2 resolution = uvec2(1920,1080);
vec3 convert(const vec2 pixelCoord){
   vec3 res = vec3(0.0,0.0,-1.0);
   vec2 pixelLocate = pixelCoord+vec2(0.5)-vec2(resolution.x/2,resolution.y/2);
   pixelLocate/=resolution.y;
   vec2 screenUV = pixelLocate*2;
   return vec3(screenUV.xy,-1.0);
}

vec3 skyColor(vec3 direction){
   if(direction.y > 0.0f){
     return mix(vec3(1.0f),vec3(0.0,0.6,1.0),direction.y);
     //return vec3(0.0,0.6,1.0);

   }
   else{
      return vec3(0.03f);
   }
}

struct HitInfo
{
  vec3 color;
  vec3 worldPosition;
  vec3 worldNormal;
};

HitInfo getObjectHitInfo(rayQueryEXT rayQuery){
   HitInfo result;
   const int primitiveID = rayQueryGetIntersectionPrimitiveIndexEXT(rayQuery, true);
   const uint i0 = indices[3*primitiveID+0];
   const uint i1 = indices[3*primitiveID+1];
   const uint i2 = indices[3*primitiveID+2];

   const vec3 v0 = vertices[i0];
   const vec3 v1 = vertices[i1];
   const vec3 v2 = vertices[i2];
   
   vec3 barycentrics = vec3(0.0, rayQueryGetIntersectionBarycentricsEXT(rayQuery,true));
   barycentrics.x= 1.0-barycentrics.y-barycentrics.z;

   const vec3 objectPos = v0 * barycentrics.x + v1 * barycentrics.y + v2 * barycentrics.z;
   result.worldPosition = objectPos;
   const vec3 objectNormal = normalize(cross(v1-v0,v2-v0));
   result.worldNormal = objectNormal;
   result.color = vec3(0.5) + 0.5 * result.worldNormal;;
   return result;
}

uint stepRNG(uint rngState)
{
  return rngState * 747796405 + 1;
}

// Steps the RNG and returns a floating-point value between 0 and 1 inclusive.
float stepAndOutputRNGFloat(inout uint rngState)
{
  // Condensed version of pcg_output_rxs_m_xs_32_32, with simple conversion to floating-point [0,1].
  rngState  = stepRNG(rngState);
  uint word = ((rngState >> ((rngState >> 28) + 4)) ^ rngState) * 277803737;
  word      = (word >> 22) ^ word;
  return float(word) / 4294967295.0f;
}

void main()
{
  const uvec2 pixel = gl_GlobalInvocationID.xy;
  if((pixel.x >= resolution.x)||(pixel.y >=resolution.y)){
      return ;
  }
  uint rngState = resolution.x*pixel.y +pixel.x;
  const vec3 cameraOrigin = vec3(-0.001,1.0,6.0);
  const float fovVerticalSlope = 1.0/5.0;
  vec3 summedPixelColor = vec3(0.0);

  const int NUM_SAMPLES = 6400;
   vec3 pixelColor = vec3(0);
  for(int sampleIdx = 0;sampleIdx<NUM_SAMPLES;sampleIdx++){
      vec3 rayOrigin = cameraOrigin;
      const vec2 randomPixelCenter = vec2(pixel)+vec2(stepAndOutputRNGFloat(rngState), stepAndOutputRNGFloat(rngState));
      const vec2 screenUV = vec2((2.0*randomPixelCenter.x-resolution.x)/resolution.y,
                                 -(2.0*randomPixelCenter.y-resolution.y)/resolution.y);
      vec3 rayDirection = vec3(fovVerticalSlope*screenUV.x, fovVerticalSlope*screenUV.y, -1.0);
      rayDirection = normalize(rayDirection);

      vec3 accumulatedRayColor = vec3(1.0);

      for(int tracedSegments = 0; tracedSegments < 15; tracedSegments++)
      {
         // Trace the ray and see if and where it intersects the scene!
         // First, initialize a ray query object:
         rayQueryEXT rayQuery;
         rayQueryInitializeEXT(rayQuery,              // Ray query
                              tlas,                  // Top-level acceleration structure
                              gl_RayFlagsOpaqueEXT,  // Ray flags, here saying "treat all geometry as opaque"
                              0xFF,                  // 8-bit instance mask, here saying "trace against all instances"
                              rayOrigin,             // Ray origin
                              0.0,                   // Minimum t-value
                              rayDirection,          // Ray direction
                              10000.0);              // Maximum t-value

         // Start traversal, and loop over all ray-scene intersections. When this finishes,
         // rayQuery stores a "committed" intersection, the closest intersection (if any).
         while(rayQueryProceedEXT(rayQuery))
         {
         }

         // Get the type of committed (true) intersection - nothing, a triangle, or
         // a generated object
         if(rayQueryGetIntersectionTypeEXT(rayQuery, true) == gl_RayQueryCommittedIntersectionTriangleEXT)
         {
         // Ray hit a triangle
         HitInfo hitInfo = getObjectHitInfo(rayQuery);

         // Apply color absorption
         accumulatedRayColor *= hitInfo.color;

         // Flip the normal so it points against the ray direction:
         hitInfo.worldNormal = faceforward(hitInfo.worldNormal, rayDirection, hitInfo.worldNormal);

         // Start a new ray at the hit position, but offset it slightly along the normal:
         rayOrigin = hitInfo.worldPosition + 0.0001 * hitInfo.worldNormal;

         // For a random diffuse bounce direction, we follow the approach of
         // Ray Tracing in One Weekend, and generate a random point on a sphere
         // of radius 1 centered at the normal. This uses the random_unit_vector
         // function from chapter 8.5:
         const float theta = 6.2831853 * stepAndOutputRNGFloat(rngState);  // Random in [0, 2pi]
         const float u     = 2.0 * stepAndOutputRNGFloat(rngState) - 1.0;  // Random in [-1, 1]
         const float r     = sqrt(1.0 - u * u);
         rayDirection      = hitInfo.worldNormal + vec3(r * cos(theta), r * sin(theta), u);
         // Then normalize the ray direction:
         rayDirection = normalize(rayDirection);
         }
         else
         {
            // Ray hit the sky
            accumulatedRayColor *= skyColor(rayDirection);
            break;
         }

      }
      pixelColor = mix(pixelColor, accumulatedRayColor,1.0/float(sampleIdx+1));
  }
   uint linearIndex = resolution.x * pixel.y + pixel.x;
   // Give the pixel the color (t/10, t/10, t/10):
   imageData[linearIndex] = pixelColor;
}