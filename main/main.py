'''
File duplicate finder. Special mode for comparing images too.
Created on 19-Feb-2021
@author: navin
'''

# if running in py3, change the shebang, drop the next import for readability (it does no harm in py3)
from __future__ import print_function   # py2 compatibility
from collections import defaultdict
import hashlib
import os
import sys


class FileOperations:
    def __init__(self):
        self.FULL_FOLDER_PATH = 0
        #self.SUBDIRECTORIES = 1
        self.FILES_IN_FOLDER = 2
    
    def getNames(self, folderToConsider): #returns as [fullFolderPath1, fullFolderPath2, ...], [[filename1, filename2, filename3, ...], [], []]
        #TODO: check if folder exists
        #TODO: what about symlinks?
        #TODO: read-only files, files without permission to read, files that can't be moved, corrupted files
        #TODO: What about "file-like objects"
        folderPaths = []; filesInFolder = []; fileSizes = []
        result = os.walk(folderToConsider)        
        for oneFolder in result:
            folderPath = oneFolder[self.FULL_FOLDER_PATH]
            folderPaths.append(folderPath)
            #subdir = oneFolder[self.SUBDIRECTORIES]
            filesInThisFolder = oneFolder[self.FILES_IN_FOLDER]
            sizeOfFiles = []
            for filename in filesInThisFolder:
                fileProperties = os.stat(folderPath + filename)
                sizeOfFiles.append(fileProperties.st_size)
            fileSizes.append(sizeOfFiles)
            filesInFolder.append(filesInThisFolder)
            
        return folderPaths, filesInFolder, fileSizes


# def chunk_reader(fobj, chunk_size=1024):
#     """Generator that reads a file in chunks of bytes"""
#     while True:
#         chunk = fobj.read(chunk_size)
#         if not chunk:
#             return
#         yield chunk
# 
# 
# def get_hash(filename, first_chunk_only=False, hash=hashlib.sha1):
#     hashobj = hash()
#     file_object = open(filename, 'rb')
# 
#     if first_chunk_only:
#         hashobj.update(file_object.read(1024))
#     else:
#         for chunk in chunk_reader(file_object):
#             hashobj.update(chunk)
#     hashed = hashobj.digest()
# 
#     file_object.close()
#     return hashed
# 
# 
# def check_for_duplicates(paths, hash=hashlib.sha1):
#     hashes_by_size = defaultdict(list)  # dict of size_in_bytes: [full_path_to_file1, full_path_to_file2, ]
#     hashes_on_1k = defaultdict(list)  # dict of (hash1k, size_in_bytes): [full_path_to_file1, full_path_to_file2, ]
#     hashes_full = {}   # dict of full_file_hash: full_path_to_file_string
# 
#     for path in paths:
#         for dirpath, dirnames, filenames in os.walk(path):
#             # get all files that have the same size - they are the collision candidates
#             for filename in filenames:
#                 full_path = os.path.join(dirpath, filename)
#                 try:
#                     # if the target is a symlink (soft one), this will 
#                     # dereference it - change the value to the actual target file
#                     full_path = os.path.realpath(full_path)
#                     file_size = os.path.getsize(full_path)
#                     hashes_by_size[file_size].append(full_path)
#                 except (OSError,):
#                     # not accessible (permissions, etc) - pass on
#                     continue
# 
# 
#     # For all files with the same file size, get their hash on the 1st 1024 bytes only
#     for size_in_bytes, files in hashes_by_size.items():
#         if len(files) < 2:
#             continue    # this file size is unique, no need to spend CPU cycles on it
# 
#         for filename in files:
#             try:
#                 small_hash = get_hash(filename, first_chunk_only=True)
#                 # the key is the hash on the first 1024 bytes plus the size - to
#                 # avoid collisions on equal hashes in the first part of the file
#                 # credits to @Futal for the optimization
#                 hashes_on_1k[(small_hash, size_in_bytes)].append(filename)
#             except (OSError,):
#                 # the file access might've changed till the exec point got here 
#                 continue
# 
#     # For all files with the hash on the 1st 1024 bytes, get their hash on the full file - collisions will be duplicates
#     for __, files_list in hashes_on_1k.items():
#         if len(files_list) < 2:
#             continue    # this hash of fist 1k file bytes is unique, no need to spend cpy cycles on it
# 
#         for filename in files_list:
#             try: 
#                 full_hash = get_hash(filename, first_chunk_only=False)
#                 duplicate = hashes_full.get(full_hash)
#                 if duplicate:
#                     print("Duplicate found: {} and {}".format(filename, duplicate))
#                 else:
#                     hashes_full[full_hash] = filename
#             except (OSError,):
#                 # the file access might've changed till the exec point got here 
#                 continue




#     if sys.argv[1:]:
#         check_for_duplicates(sys.argv[1:])
#     else:
#         print("Please pass the paths to check as parameters to the script")


# import os, sys
# import Image
# 
# im = Image.open("image.jpg")
# x = 3
# y = 4
# 
# pix = im.load()
# print pix[x,y]
# #------
# photo = Image.open('IN.jpg') #your image
# photo = photo.convert('RGB')
# 
# width = photo.size[0] #define W and H
# height = photo.size[1]
# 
# for y in range(0, height): #each pixel has coordinates
#     row = ""
#     for x in range(0, width):
# 
#         RGB = photo.getpixel((x,y))
#         R,G,B = RGB  #now you can use the RGB value
    
    
    
    