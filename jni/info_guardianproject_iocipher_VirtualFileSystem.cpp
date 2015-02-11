
#define LOG_TAG "VirtualFileSystem.cpp"

#include "JNIHelp.h"
#include "JniConstants.h"
#include "ScopedUtfChars.h"

#include "sqlfs.h"

#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>

// yes, dbFileName is a duplicate of default_db_file in sqlfs.c
char dbFileName[PATH_MAX] = { 0 };
// store first sqlfs instance as marker for mounted state
static sqlfs_t *sqlfs = NULL;

void setDatabaseFileName(JNIEnv *env, jobject obj) {
    jclass cls = env->GetObjectClass(obj);
    jfieldID fid = env->GetStaticFieldID(cls, "dbFileName", "Ljava/lang/String;");
    jstring javaDbFileName = (jstring) env->GetStaticObjectField(cls, fid);
    const char *name = env->GetStringUTFChars(javaDbFileName, 0);
    memset(dbFileName, 0, PATH_MAX);
    if (name == NULL)
        return;
    strncpy(dbFileName, name, PATH_MAX-2);
    dbFileName[PATH_MAX-1] = '\0';
    env->ReleaseStringUTFChars(javaDbFileName, name);
}

void checkMountProblem(JNIEnv *env, char* dbFileName) {
    char msg[256];
    if (access(dbFileName, R_OK) != 0) {
        snprintf(msg, 255,
                 "Could not mount %s does not exist or is not readable (%d)!",
                 dbFileName, errno);
    } else if (access(dbFileName, W_OK) != 0) {
        snprintf(msg, 255,
                 "Could not mount %s is not writable (%d)!",
                 dbFileName, errno);
    } else {
        snprintf(msg, 255,
                 "Could not mount filesystem in %s, bad password given?", dbFileName);
    }
    jniThrowException(env, "java/lang/IllegalArgumentException", msg);
}

static jboolean VirtualFileSystem_isMounted(JNIEnv *env, jobject obj) {
    return sqlfs != NULL || sqlfs_instance_count() > 0;
}

static void VirtualFileSystem_mount_unencrypted(JNIEnv *env, jobject obj) {
    setDatabaseFileName(env, obj);
    if (!sqlfs_open(dbFileName, &sqlfs)) {
        char msg[256];
        snprintf(msg, 255, "Could not mount filesystem in '%s'", dbFileName);
        jniThrowException(env, "java/lang/IllegalArgumentException", msg);
    }
}

static void VirtualFileSystem_mount(JNIEnv *env, jobject obj, jstring javaPassword) {
    char msg[256];
    if (VirtualFileSystem_isMounted(env, obj)) {
        snprintf(msg, 255, "Filesystem in '%s' already mounted!", dbFileName);
        jniThrowException(env, "java/lang/IllegalStateException", msg);
        return;
    }

    setDatabaseFileName(env, obj);

    char const *password = env->GetStringUTFChars(javaPassword, 0);
    jsize passwordLen = env->GetStringUTFLength(javaPassword);

    /* Attempt to open the database with the password, then immediately close
     * it. If it fails, then the password is likely wrong. */
    if (!sqlfs_open_password(dbFileName, password, &sqlfs)) {
        checkMountProblem(env, dbFileName);
    }
    env->ReleaseStringUTFChars(javaPassword, password);
}

static void VirtualFileSystem_mount_byte(JNIEnv *env, jobject obj, jbyteArray javaKey) {
    char msg[256];
    if (VirtualFileSystem_isMounted(env, obj)) {
        snprintf(msg, 255, "Filesystem in '%s' already mounted!", dbFileName);
        jniThrowException(env, "java/lang/IllegalStateException", msg);
        return;
    }

    jsize keyLen = env->GetArrayLength(javaKey);
    if (keyLen != REQUIRED_KEY_LENGTH) {
        snprintf(msg, 255, "Key length is not %i bytes (%i bytes)!",
                 REQUIRED_KEY_LENGTH, keyLen);
        jniThrowException(env, "java/lang/IllegalArgumentException", msg);
        return;
    }

    setDatabaseFileName(env, obj);
    jbyte *key = env->GetByteArrayElements(javaKey, NULL); //direct mem ref

    /* attempt to open the database with the key if it fails, most likely the
     * db file does not exist or the key is wrong */
    if (!sqlfs_open_key(dbFileName, (uint8_t*)key, keyLen, &sqlfs)) {
        checkMountProblem(env, dbFileName);
    }

    env->ReleaseByteArrayElements(javaKey, key, 0);
}

static void VirtualFileSystem_unmount(JNIEnv *env, jobject obj) {
    char msg[256];
    if (!VirtualFileSystem_isMounted(env, obj)) {
        snprintf(msg, 255, "Filesystem in '%s' not mounted!", dbFileName);
        jniThrowException(env, "java/lang/IllegalStateException", msg);
        return;
    }
    /* VFS holds a sqlfs instance open purely as a marker of the filesystem
     * being mounted. If there is more than one sqlfs instance, that means
     * that threads are still active. Closing the final sqlfs instances causes
     * libsqlfs to close the database and zero out the key/password. */
    if (sqlfs_instance_count() > 1) {
        snprintf(msg, 255,
                 "Cannot unmount when threads are still active! (%i threads)",
                 sqlfs_instance_count() - 1);
        jniThrowException(env, "java/lang/IllegalStateException", msg);
        return;
    }
    sqlfs_close(sqlfs);
    sqlfs = NULL;
    memset(dbFileName, 0, PATH_MAX);
}

static void VirtualFileSystem_beginTransaction(JNIEnv *env, jobject) {
    sqlfs_begin_transaction(0);
    return;
}

static void VirtualFileSystem_completeTransaction(JNIEnv *env, jobject) {
    sqlfs_complete_transaction(0,1);
    return;
}

static JNINativeMethod sMethods[] = {
    {"mount_unencrypted", "()V", (void *)VirtualFileSystem_mount_unencrypted},
    {"mount", "(Ljava/lang/String;)V", (void *)VirtualFileSystem_mount},
    {"mount", "([B)V", (void *)VirtualFileSystem_mount_byte},
    {"unmount", "()V", (void *)VirtualFileSystem_unmount},
    {"isMounted", "()Z", (void *)VirtualFileSystem_isMounted},
    {"beginTransaction", "()V", (void *)VirtualFileSystem_beginTransaction},
    {"completeTransaction", "()V", (void *)VirtualFileSystem_completeTransaction},
};
int register_info_guardianproject_iocipher_VirtualFileSystem(JNIEnv* env) {
    jclass cls = env->FindClass("info/guardianproject/iocipher/VirtualFileSystem");
    if (cls == NULL) {
        LOGE("Can't find info/guardianproject/iocipher/VirtualFileSystem\n");
        return -1;
    }
    return env->RegisterNatives(cls, sMethods, NELEM(sMethods));
}
