# Cover Generation System

* Cover Generation system is located in CoverGen.cpp & CoverGen.h

Debug info:
* Green nodes represent dynamic cover
* Blue nodes represent static cover 
* Red nodes represent nodes that were removed during the optimization process.
* Yellow arrows represent connections between nodes.
* White diamond above an object represents that the object is using cover from geometry and not from its bounds (currently, we generate cover using object's bounds but with more complicated geometry that might be problematic so we can add the "CoverFromGeometry" tag to use geometry data to generate cover)
* Object's with no cover are using the "NoCover" tag

Video: https://youtu.be/HnNUtj6y6vA
