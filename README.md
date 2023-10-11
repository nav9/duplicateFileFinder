![Alt text](images/initialScreen.png?raw=true "Main menu")  
![Alt text](images/folderChoice.png?raw=true "Choosing the folder")  
  
# Duplicate File Finder  
* File duplicates: This program finds duplicate files in a folder and its subfolders. Duplicates are moved to a separate folder. Files are not checked based on filenames. They are checked based on file size and chunks of binary data that the file is composed of. Unlike other programs that calculate the hashes of files (which can get very expensive for large files), this program starts with comparing small binary chunks, which also helps quickly eliminate files that don't match.
* Image duplicates: There is also a mode to find image duplicates based on similarity of pixels (perceptual hash), even if the images are in different file formats (the same image present as png and jpg) or varied in size (Eg: the same image present as 640x480 and 1024x768). Image formats currently tested are jpg, png and webp. There is a lot more to be programmed to make this fully functional. The TODO's are mentioned in the code.
* Line duplicates: If the same lines are present in multiple text files or a single text file, this program will take all unique lines and write them to a new file.  
* Deleting residual files: The program can be run in a second mode that searches for files of a specified name, and deletes them in the folder and subfolders of a specified directory. For example, it's useful in deleting multiple Thumbs.db files. The User is given a choice of whether the search should be case sensitive or not.
* Undo option: After duplicate files are moved into a folder, if the user wants to undo the operation and restore all files to their original folders and filenames, it is possible by using an undo file that gets created during the duplciate finding. The user can also refer the undo file and manually undo.

  
# To run the program  
`Python 3.9.6` was used. You could use PyEnv or Anaconda/Miniconda to install specific versions of Python. For Python and the pip packages, it isn't strictly necessary to install the specified versions. The versions have been specified to help prevent incompatibility issues that may be present in future versions. So by knowing what versions the working version of `duplicateFileFinder` used, you'll be able to run the program without hassles.     
  
First install dependencies, using (the version numbers don't have to match exactly):  
`pip3 install imagehash==4.3.0 PySimpleGUI==4.60.3 Pillow==9.2.0`    
  
Then simply run the main file as:  
`python3 main.py`  
  
# To run the tests  
`pip3 install pytest==7.1.2`  
Use `pytest` at the commandline to run the test cases. Do this in the root program directory.  
  
# TODO
These are mentioned in the code for now.

# Known issues
* Menus could be made more generic and multiple menus can be presented in a single window rather than have to go through multiple menus.
* File/folder error handling needs to be more robust.
* GUI aesthetics needs improvement.
* Code needs to be broken further into discrete, standalone components.

# Additional info  
* The GUI library used is `PySimpleGUI`.
* Images are compared with what's called an "image hash". It's different from standard hashing algorithms. The imagehash algorithm documentation explains more about it. More info: https://pypi.org/project/ImageHash/
  
[![Donate](https://raw.githubusercontent.com/nav9/VCF_contacts_merger/main/gallery/thankYouDonateButton.png)](https://nrecursions.blogspot.com/2020/08/saying-thank-you.html)    
