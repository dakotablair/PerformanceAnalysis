"""
StrmKMeans - This is the streaming kmeans algorithm generated based on the NIPS2011 Paper. 
For more information, please check
http://papers.nips.cc/paper/4362-fast-and-accurate-k-means-for-large-datasets.pdf 


Author: Xi Zhang (xizhang1@cs.stonybrook.edu)

Created : Oct, 2017
Modified: May, 2018 (Add DataType to the algorithm)
"""

import numpy as np 
from numpy import linalg as LA
from sklearn.preprocessing import normalize
from sklearn.metrics.pairwise import cosine_similarity

from random import *

def StrmKMmeans(x, Centroids, Assign, cof_f, max_Cen, beta, DataType = 'Otherwise'):
	""" Streaming K-means clustering algorithm

	Parameters:
	----------------------------
	point_current : float ndarray with shape (n_features, )
		The observation to cluster.

	Centroids : float ndarray with shape (n_clusters, 2*n_features+1)
		Previous centroids with 2m+1 columns and no larger than max_Cen rows. The first m
		columns is the centers and the sencond m columns is the summation of its data points 

	Assign : integer ndarray with shape (n_samples, )
		Previous assignment for the arrived data points 

	f_cof : float
		The current facility cost

	max_Cen: int
		The maximum number of clusters/facilities

	beta: float
		Scalar term

	DataType: "Text" or "Otherwise", default="Otherwise" 
		What kind of streaming data is processed. When text data is processed, each centroid
		is normalized by L2 Norm

	Returns:
	-----------------------------
	Centroids : float ndarray with shape (n_clusters, 2*n_features+1)
		The updated centroids of facilities

	Assign : integer ndarray with shape (n_samples, )
		The updated assignment of each data point

	f_cof : float
		The updated cost generated by line xxx

	"""

	m = len(x)

	## When facility set is empty, initialize
	if not Centroids.size: 
		#print "Entered the first time!"
		x = np.append(x, x)
		x = np.append(x, 1)
		Centroids = x
		Centroids = x.reshape(1, len(x))
		Assign = np.append(Assign, 0)

	## When some facilities exist
	else: 
		# Find the nearest facility of current point x and assign delta = min(distance)
		Dist = np.sum((Centroids[:,:m] - x)**2, axis = 1)
		delta_y = np.min(Dist)
		idx_y = np.argmin(Dist)

		# If prob(delta/f) occurs, which means the current point x is too far away from all the facilities, 
		# then makes it a new facility; 
		# otherwise, assign it to its closest facility, which index is idx_y
		if random()<delta_y/cof_f: # Create new facility for x
			x = np.append(x, x)
			x = np.append(x, 1)
			Centroids = np.append( Centroids, [x], axis = 0 )
			Assign = np.append(Assign, Centroids.shape[0]-1)
		else: # Add x to an existing facility
			Centroids[idx_y, m:2*m] = Centroids[idx_y, m:2*m] + x
			Centroids[idx_y, 2*m] += 1
			Assign =  np.append(Assign, idx_y)
			# print Assign

	## When the number of facilities is larger than the certain constraint max_Cen
	while Centroids.shape[0]>max_Cen: 
		cof_f = beta * cof_f;

		# Move each centroid to the center of mass of its points 
		for i in range(Centroids.shape[0]):
			if DataType == 'Text':
				Centroids[i, :m] = Centroids[i, m:2*m]/Centroids[i, 2*m]
				Centroids[i, :m] = Centroids[i, m:2*m]/LA.norm(Centroids[i, m:2*m])
			else:
				Centroids[i, :m] = Centroids[i, m:2*m]/Centroids[i, 2*m]
		
		# initial C_til to be the first facility
		Centroids_til = Centroids[0, :]
		Centroids_til = Centroids_til.reshape(1, len(Centroids_til))

		for i in range(1, Centroids.shape[0]): # for each z in the preivous facility C
			
			# find the closest center of current facility inside C_til, which index is idx_y point_z
			Dist_til = np.sum((Centroids_til[:,:m] - Centroids[i, :m])**2, axis = 1)
			delta_z = np.min(Dist_til)
			idx_z = np.argmin(Dist_til)

			# If prob(w_x*delta/f) occurs, make it as a new facility;
			# otherwise, merge it to its closest facility in C_til
			if random()<(delta_z*Centroids[i, 2*m]/cof_f):
				# print "Append the", i, "facility"
				Centroids_til = np.append(Centroids_til, [Centroids[i, :]], axis = 0)
			else:
				# print "Add the", i, "facility to the existing Centroids"
				Assign[Assign[:]==i] = idx_z
				Centroids_til[idx_z, m:2*m] = Centroids_til[idx_z, m:2*m] + Centroids[i, m:2*m]
				Centroids_til[idx_z, 2*m] += Centroids[i, 2*m]
				
		Centroids = Centroids_til

	return Centroids, Assign, cof_f


"""
x = np.array([[0.5,0.5], [0.3,0.2], [-1,-1], [2,1], [1,-1], [2,2], [3,5]])
Centroids = np.array([])
Assign = []

for i in range(0, x.shape[0]):
	print "Step", i, ": current point is ", x[i]
	[Centroids, Assign, cof_f] = StrmKMmeans(x[i], Centroids, Assign, 10.0, 2, 5, DataType = 'Text')
	print "Step", i, "finished!"
	print Centroids, Assign
"""