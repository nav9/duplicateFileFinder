![Alt text](images/initialScreen.png?raw=true "Main menu")  
![Alt text](images/folderChoice.png?raw=true "Choosing the folder")  
  
# Duplicate File Finder  
* This program finds duplicate files in a folder and its subfolders. Duplicates are moved to a separate folder. Files are not checked based on filenames. They are checked based on file size and chunks of binary data that the file is composed of. Unlike other programs that calculate the hashes of files (which can get very expensive for large files), this program starts with comparing small binary chunks, which also helps quickly eliminate files that don't match.
* The program can be run in a second mode that searches for files of a specified name, and deletes them in the folder and subfolders of a specified directory. For example, it's useful in deleting multiple Thumbs.db files. The User is given a choice of whether the search should be case sensitive or not.
* There is also a mode to find image duplicates based on similarity of pixels, even if the images are in different file formats (the same image present as png and jpg) or varied in size (Eg: the same image present as 640x480 and 1024x768). There is a lot more to be programmed to make this fully functional. The TODO"s are mentioned in the code.

    
# To run the program  
First install imagehash, using:  
pip3 install imagehash  
  
Then simply run the main file as:  
python3 main.py  
  
# TODO
These are mentioned in the code for now.

# Known issues
* Menus could be made more generic and multiple menus can be presented in a single window rather than have to go through multiple menus.
* File/folder error handling needs to be more robust.
* GUI aesthetics needs improvement.
* Code needs to be broken further into discrete, standalone components.

# Additional info
* The project was created with LiClipse (not Eclipse. LiClipse is a nice IDE that has PyDev and supports refactoring and auto-complete for Python) in Python3.
* The GUI library used is PySimpleGUI.
  
  
