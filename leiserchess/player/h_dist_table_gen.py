def h_dist(delta_fil, delta_rnk):
	return float(str(round((1.0/(delta_fil+1)) + (1.0/(delta_rnk+1)), 8)))

table = [[h_dist(i,j) for i in range(8)] for j in range(8)]
print(table)