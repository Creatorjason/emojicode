//
//  main.cpp
//  EmojicodeCompiler
//
//  Created by Theo Weidmann on 05/09/2017.
//  Copyright © 2017 Theo Weidmann. All rights reserved.
//

#include "Runtime.h"
#include "Internal.hpp"
#include <cinttypes>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <random>

runtime::internal::ControlBlock ejcIgnoreBlock;

int runtime::internal::argc;
char **runtime::internal::argv;
int runtime::internal::seed;

runtime::internal::ControlBlock* runtime::internal::newControlBlock() {
    return new runtime::internal::ControlBlock;
}

extern "C" runtime::Integer fn_1f3c1();

extern "C" int8_t* ejcAlloc(runtime::Integer size) {
    auto ptr = malloc(size);
    *static_cast<runtime::internal::ControlBlock**>(ptr) = new runtime::internal::ControlBlock;
    return static_cast<int8_t*>(ptr);
}

extern "C" void ejcRetain(runtime::Object<void> *object) {
    runtime::internal::ControlBlock *controlBlock = object->controlBlock();
    if (controlBlock == nullptr) {
        auto ptr = reinterpret_cast<int64_t *>(reinterpret_cast<uint8_t *>(object) - 8);
        (*ptr)++;
        return;
    }
    if (controlBlock == &ejcIgnoreBlock) return;
    controlBlock->strongCount.fetch_add(1, std::memory_order_relaxed);
}

extern "C" void ejcRetainMemory(runtime::Object<void> *object) {
    runtime::internal::ControlBlock *controlBlock = object->controlBlock();
    if (controlBlock == &ejcIgnoreBlock) return;
    controlBlock->strongCount.fetch_add(1, std::memory_order_relaxed);
}

bool releaseLocal(void *object) {
    auto &ptr = *reinterpret_cast<int64_t *>(reinterpret_cast<uint8_t *>(object) - 8);
    ptr--;
    return ptr == 0;
}

extern "C" void ejcReleaseLocal(runtime::Object<void> *object) {
    if (releaseLocal(object)) {
        object->classInfo()->destructor(object);
    }
}

void deleteControlBlock(runtime::internal::ControlBlock *block) {
    if (block->weakCount == 0) {
        delete block;
    }
}

extern "C" void ejcRelease(runtime::Object<void> *object) {
    runtime::internal::ControlBlock *controlBlock = object->controlBlock();
    if (controlBlock == nullptr) {
        if (releaseLocal(object)) {
            object->classInfo()->destructor(object);
        }
        return;
    }
    if (controlBlock == &ejcIgnoreBlock) return;

    if (controlBlock->strongCount.fetch_sub(1, std::memory_order_acq_rel) - 1 != 0) return;

    object->classInfo()->destructor(object);
    deleteControlBlock(controlBlock);
    free(object);
}

extern "C" void ejcReleaseCapture(runtime::internal::Capture *capture) {
    runtime::internal::ControlBlock *controlBlock = capture->controlBlock;
    if (controlBlock == nullptr) {
        if (releaseLocal(capture)) {
            capture->deinit(capture);
        }
        return;
    }

    if (controlBlock->strongCount.fetch_sub(1, std::memory_order_acq_rel) - 1 != 0) return;

    capture->deinit(capture);
    deleteControlBlock(controlBlock);
    free(capture);
}

extern "C" void ejcReleaseMemory(runtime::Object<void> *object) {
    runtime::internal::ControlBlock *controlBlock = object->controlBlock();

    if (controlBlock == &ejcIgnoreBlock) return;

    if (controlBlock->strongCount.fetch_sub(1, std::memory_order_acq_rel) - 1 != 0) return;

    delete controlBlock;
    free(object);
}

extern "C" void ejcReleaseWithoutDeinit(runtime::Object<void> *object) {
    runtime::internal::ControlBlock *controlBlock = object->controlBlock();
    if (controlBlock == nullptr) {
        releaseLocal(object);
        return;
    }
    if (controlBlock->strongCount.fetch_sub(1, std::memory_order_acq_rel) - 1 != 0) return;

    deleteControlBlock(controlBlock);
    free(object);
}

struct WeakReference {
    runtime::internal::ControlBlock *block;
    void *object;
};

void releaseWeakReference(WeakReference *ref) {
    ref->block->weakCount--;
    if (ref->block->strongCount == 0) {
        deleteControlBlock(ref->block);
    }
    ref->block = nullptr;
}

extern "C" void ejcCreateWeak(WeakReference *ref, runtime::Object<void> *object) {
    ref->object = object;
    object->controlBlock()->weakCount++;
    ref->block = object->controlBlock();
}

extern "C" void ejcRetainWeak(WeakReference *ref) {
    if (ref->block != nullptr) {
        ref->block->weakCount++;
    }
}

extern "C" void ejcReleaseWeak(WeakReference *ref) {
    if (ref->block != nullptr) {
        releaseWeakReference(ref);
    }
}

extern "C" runtime::SimpleOptional<void*> ejcAcquireStrong(WeakReference *ref) {
    if (ref->block == nullptr) {
        return runtime::NoValue;
    }
    if (ref->block->strongCount == 0) {
        releaseWeakReference(ref);
        return runtime::NoValue;
    }
    ref->block->strongCount++;
    return ref->object;
}

extern "C" bool ejcInheritsFrom(runtime::ClassInfo *classInfo, runtime::ClassInfo *from) {
    for (auto classInfoNew = classInfo; classInfoNew != nullptr; classInfoNew = classInfoNew->superclass) {
        if (classInfoNew == from) {
            return true;
        }
    }
    return false;
}

struct ProtocolConformanceEntry {
    void *protocolId;
    void *protocolConformance;
};

extern "C" void* ejcFindProtocolConformance(ProtocolConformanceEntry *info, void *protocolId) {
    for (auto infoNew = info; infoNew->protocolId != nullptr; infoNew++) {
        if (infoNew->protocolId == protocolId) {
            return infoNew->protocolConformance;
        }
    }
    return nullptr;
}

struct RunTimeTypeInfo {
    int16_t paramCount;
    int16_t paramOffset;
};

struct TypeDescription {
    RunTimeTypeInfo *rtti;
    bool optional;
};

bool checkGenericArgs(TypeDescription **argsl, TypeDescription **argsr, int16_t argsCount, int16_t argsOffset) {
    *argsl += argsOffset, *argsr += argsOffset;
    for (int16_t i = 0; i < argsCount; i++) {
        auto l = *((*argsl)++), r = *((*argsr)++);
        if (l.rtti != r.rtti || l.optional != r.optional) return false;
        if (!checkGenericArgs(argsl, argsr, l.rtti->paramCount, l.rtti->paramOffset)) return false;
    }
    return true;
}

extern "C" bool ejcCheckGenericArgs(TypeDescription *argsl, TypeDescription *argsr, int16_t argsCount,
                                    int16_t argsOffset) {
    auto g = checkGenericArgs(&argsl, &argsr, argsCount, argsOffset);
    return g;
}

extern "C" runtime::Integer ejcTypeDescriptionLength(TypeDescription *arg) {
    runtime::Integer count = 1;
    for (runtime::Integer i = 0; i < count; i++) {
        count += (arg++)->rtti->paramCount;
    }
    return count;
}

extern "C" TypeDescription* ejcIndexTypeDescription(TypeDescription *arg, runtime::Integer index) {
    for (runtime::Integer i = 0; i < index; i++) {
        index += (arg++)->rtti->paramCount;
    }
    return arg;
}

extern "C" void ejcMemoryRealloc(int8_t **pointerPtr, runtime::Integer newSize) {
    *pointerPtr = static_cast<int8_t*>(realloc(*pointerPtr, newSize + sizeof(runtime::internal::ControlBlock*)));
}

extern "C" runtime::Integer ejcMemoryCompare(int8_t **self, int8_t *other, runtime::Integer bytes) {
    return std::memcmp(*self + sizeof(runtime::internal::ControlBlock*),
                       other + sizeof(runtime::internal::ControlBlock*), bytes);
}

extern "C" bool ejcIsOnlyReference(runtime::Object<void> *object) {
    runtime::internal::ControlBlock *controlBlock = object->controlBlock();
    if (controlBlock == nullptr) {
        return *reinterpret_cast<int64_t *>(reinterpret_cast<uint8_t *>(object) - 8) == 1;
    }
    if (controlBlock == &ejcIgnoreBlock) return false;  // Impossible to say as object is not reference counted
    return controlBlock->strongCount == 1;
}

extern "C" [[noreturn]] void ejcPanic(const char *message) {
    std::cout << "🤯 Program panicked: " << message << std::endl;
    abort();
}

int main(int largc, char **largv) {
    runtime::internal::argc = largc;
    runtime::internal::argv = largv;
    runtime::internal::seed = std::random_device()();

    auto code = fn_1f3c1();
    return static_cast<int>(code);
}
